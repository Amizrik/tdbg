"""tdbg: QR Code, Data Matrix, Aztec and MaxiCode generation backed by a C library.

    >>> import tdbg
    >>> sym = tdbg.qr("https://example.com", ecc="M")
    >>> sym.save("qr.png", scale=8)        # or .svg
    >>> print(sym.to_text())               # terminal preview

The shared library is located in this order:
  1. the TDBG_LIBRARY environment variable (full path),
  2. next to this file (what `pip install` produces),
  3. the system library path (libtdbg.so / libtdbg.dylib / tdbg.dll).
"""
import ctypes
import ctypes.util
import glob
import math
import os
import struct
import sys
import zlib

__all__ = ["qr", "datamatrix", "aztec", "maxicode", "Symbol", "BarcodeError", "library_version"]

GRID_SQUARE = 0
GRID_HEX = 1


class BarcodeError(ValueError):
    """Raised when the C encoder rejects the input."""

    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


class _CSymbol(ctypes.Structure):
    _fields_ = [
        ("width", ctypes.c_int),
        ("height", ctypes.c_int),
        ("grid", ctypes.c_int),
        ("quiet_zone", ctypes.c_int),
        ("modules", ctypes.POINTER(ctypes.c_ubyte)),
    ]


def _load_library():
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = []
    env = os.environ.get("TDBG_LIBRARY")
    if env:
        candidates.append(env)
    for pattern in ("_libtdbg*.so", "_libtdbg*.pyd", "_libtdbg*.dylib",
                    "libtdbg.so", "libtdbg.dylib", "tdbg.dll", "libtdbg.dll"):
        candidates.extend(sorted(glob.glob(os.path.join(here, pattern))))
    found = ctypes.util.find_library("tdbg")
    if found:
        candidates.append(found)
    errors = []
    for path in candidates:
        try:
            return ctypes.CDLL(path)
        except OSError as exc:
            errors.append("%s: %s" % (path, exc))
    raise OSError("tdbg C library not found; build it (make / cmake / pip install .) "
                  "or set TDBG_LIBRARY.\n" + "\n".join(errors))


_lib = _load_library()
_PSYM = ctypes.POINTER(_CSymbol)
_BUF = ctypes.c_char_p

_lib.tdbg_qr_encode.argtypes = [_BUF, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.POINTER(_PSYM)]
_lib.tdbg_datamatrix_encode.argtypes = [_BUF, ctypes.c_int, ctypes.c_int, ctypes.POINTER(_PSYM)]
_lib.tdbg_aztec_encode.argtypes = [_BUF, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.POINTER(_PSYM)]
_lib.tdbg_maxicode_encode.argtypes = [ctypes.c_int, _BUF, ctypes.c_int, ctypes.c_char_p,
                                    ctypes.c_int, ctypes.c_int, ctypes.POINTER(_PSYM)]
for _f in ("tdbg_qr_encode", "tdbg_datamatrix_encode", "tdbg_aztec_encode", "tdbg_maxicode_encode"):
    getattr(_lib, _f).restype = ctypes.c_int
_lib.tdbg_symbol_free.argtypes = [_PSYM]
_lib.tdbg_symbol_free.restype = None
_lib.tdbg_svg.argtypes = [_PSYM, ctypes.c_double, ctypes.c_int]
_lib.tdbg_svg.restype = ctypes.c_void_p
_lib.tdbg_free.argtypes = [ctypes.c_void_p]
_lib.tdbg_free.restype = None
_lib.tdbg_strerror.argtypes = [ctypes.c_int]
_lib.tdbg_strerror.restype = ctypes.c_char_p
_lib.tdbg_version.argtypes = []
_lib.tdbg_version.restype = ctypes.c_char_p


def library_version():
    return _lib.tdbg_version().decode()


def _as_bytes(data, encoding="utf-8"):
    if isinstance(data, str):
        return data.encode(encoding)
    return bytes(data)


def _call(fn, *args):
    out = _PSYM()
    rc = fn(*args, ctypes.byref(out))
    if rc != 0:
        raise BarcodeError(rc, _lib.tdbg_strerror(rc).decode())
    try:
        return Symbol._from_c(out.contents)
    finally:
        _lib.tdbg_symbol_free(out)


# ----------------------------------------------------------------- symbol

_HEX_PITCH = math.sqrt(3) / 2          # row pitch of the MaxiCode grid
_HEX_R = 1 / math.sqrt(3)              # hexagon centre-to-vertex
# ISO/IEC 16023 4.2.1.1: light centre of diameter V (= 2*_HEX_R), three dark rings,
# overall diameter 9 module widths, equal diameter steps.
_BULLSEYE = tuple(_HEX_R + k * (4.5 - _HEX_R) / 5 for k in (5, 4, 3, 2, 1, 0))
_HEX_FILL = 0.94                       # hexagon size relative to its cell (same as the SVG)


class Symbol:
    """A generated barcode: a grid of modules (True = dark)."""

    def __init__(self, width, height, grid, quiet_zone, modules):
        self.width = width
        self.height = height
        self.grid = grid
        self.quiet_zone = quiet_zone
        self._m = bytes(modules)

    @classmethod
    def _from_c(cls, cs):
        n = cs.width * cs.height
        return cls(cs.width, cs.height, cs.grid, cs.quiet_zone, ctypes.string_at(cs.modules, n))

    @property
    def is_hex(self):
        return self.grid == GRID_HEX

    def __getitem__(self, xy):
        x, y = xy
        return bool(self._m[y * self.width + x])

    def rows(self):
        """List of rows, each a list of bools."""
        w = self.width
        return [[bool(v) for v in self._m[y * w:(y + 1) * w]] for y in range(self.height)]

    def __repr__(self):
        kind = "hex" if self.is_hex else "square"
        return "<tdbg.Symbol %dx%d %s>" % (self.width, self.height, kind)

    # ---- text
    def to_text(self, quiet_zone=None, invert=False):
        """Unicode half-block rendering for terminals (hex grids are approximated)."""
        qz = self.quiet_zone if quiet_zone is None else quiet_zone
        w, h = self.width + 2 * qz, self.height + 2 * qz

        def dark(x, y):
            x -= qz
            y -= qz
            if 0 <= x < self.width and 0 <= y < self.height:
                return self[x, y] != invert
            return invert

        chars = {(False, False): " ", (True, False): "▀", (False, True): "▄", (True, True): "█"}
        lines = []
        for y in range(0, h, 2):
            lines.append("".join(chars[(dark(x, y), dark(x, y + 1))] for x in range(w)))
        return "\n".join(lines)

    # ---- SVG (rendered by the C library)
    def to_svg(self, module_size=4.0, quiet_zone=None):
        qz = -1 if quiet_zone is None else int(quiet_zone)
        return _render_svg(self, module_size, qz)

    # ---- PNG (pure Python, no dependencies)
    def to_png(self, scale=4, quiet_zone=None):
        qz = self.quiet_zone if quiet_zone is None else quiet_zone
        if self.is_hex:
            rows = self._raster_hex(scale, qz)
        else:
            rows = self._raster_square(scale, qz)
        return _png_gray(rows)

    def _raster_square(self, scale, qz):
        w = self.width
        blank = b"\xff" * ((w + 2 * qz) * scale)
        out = [blank] * (qz * scale)
        for y in range(self.height):
            line = bytearray(b"\xff" * (qz * scale))
            for v in self._m[y * w:(y + 1) * w]:
                line += (b"\x00" if v else b"\xff") * scale
            line += b"\xff" * (qz * scale)
            out.extend([bytes(line)] * scale)
        out.extend([blank] * (qz * scale))
        return out

    def _raster_hex(self, scale, qz):
        W = int(math.ceil((self.width + 2 * qz) * scale))
        H = int(math.ceil(((self.height - 1) * _HEX_PITCH + 2 * _HEX_R + 2 * qz) * scale))
        cx0 = qz + 14.5
        cy0 = qz + _HEX_R + 16 * _HEX_PITCH
        ring_r = _BULLSEYE
        out = []
        for py in range(H):
            v = (py + 0.5) / scale - qz - _HEX_R
            r0 = int(round(v / _HEX_PITCH))
            line = bytearray(b"\xff" * W)
            for px in range(W):
                u = (px + 0.5) / scale - qz
                # bullseye has priority in its area
                d = math.hypot(u + qz - cx0, v + qz + _HEX_R - cy0)
                if d < ring_r[0]:
                    k = sum(1 for r in ring_r if d < r)
                    if k % 2 == 1:
                        line[px] = 0
                    continue
                best = None
                for r in (r0 - 1, r0, r0 + 1):
                    if not 0 <= r < self.height:
                        continue
                    c = int(round(u - 0.5 - (r & 1) * 0.5))
                    for cc in (c - 1, c, c + 1):
                        if not 0 <= cc < self.width:
                            continue
                        dx = u - (cc + 0.5 + (r & 1) * 0.5)
                        dy = v - r * _HEX_PITCH
                        dd = dx * dx + dy * dy
                        if best is None or dd < best[0]:
                            best = (dd, cc, r)
                if best and self._m[best[2] * self.width + best[1]]:
                    # inside the (slightly shrunk) pointy-top hexagon around that centre?
                    dx = abs(u - (best[1] + 0.5 + (best[2] & 1) * 0.5)) / _HEX_FILL
                    dy = abs(v - best[2] * _HEX_PITCH) / _HEX_FILL
                    if dx <= 0.5 and dy <= _HEX_R - dx * _HEX_R:
                        line[px] = 0
            out.append(bytes(line))
        return out

    def save(self, path, scale=4, quiet_zone=None):
        """Write .png or .svg depending on the file extension."""
        ext = os.path.splitext(path)[1].lower()
        if ext == ".svg":
            data = self.to_svg(module_size=scale, quiet_zone=quiet_zone).encode()
        elif ext == ".png":
            data = self.to_png(scale=scale, quiet_zone=quiet_zone)
        else:
            raise ValueError("unsupported extension %r (use .png or .svg)" % ext)
        with open(path, "wb") as f:
            f.write(data)
        return path

    def to_pil(self, scale=4, quiet_zone=None):
        """Return a Pillow image (requires Pillow)."""
        import io
        from PIL import Image
        return Image.open(io.BytesIO(self.to_png(scale, quiet_zone)))


def _render_svg(sym, module_size, qz):
    buf = ctypes.create_string_buffer(sym._m, len(sym._m))
    cs = _CSymbol(sym.width, sym.height, sym.grid, sym.quiet_zone,
                  ctypes.cast(buf, ctypes.POINTER(ctypes.c_ubyte)))
    p = _lib.tdbg_svg(ctypes.byref(cs), float(module_size), qz)
    if not p:
        raise MemoryError("tdbg_svg failed")
    try:
        return ctypes.string_at(p).decode()
    finally:
        _lib.tdbg_free(p)


def _png_gray(rows):
    h = len(rows)
    w = len(rows[0]) if h else 0

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + r for r in rows)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))


# ------------------------------------------------------------- encoders

_QR_ECC = {"L": 0, "M": 1, "Q": 2, "H": 3}
_DM_SHAPE = {"square": 0, "rect": 1, "rectangle": 1, "any": 2}


def qr(data, ecc="M", version=0, mask=-1, encoding="utf-8"):
    """QR Code. ecc: L/M/Q/H; version: 0=auto or minimum 1-40; mask: -1=auto or 0-7."""
    b = _as_bytes(data, encoding)
    e = _QR_ECC[ecc.upper()] if isinstance(ecc, str) else int(ecc)
    return _call(_lib.tdbg_qr_encode, b, len(b), e, int(version), int(mask))


def datamatrix(data, shape="square", encoding="utf-8"):
    """Data Matrix ECC 200. shape: 'square', 'rect' or 'any'."""
    b = _as_bytes(data, encoding)
    s = _DM_SHAPE[shape] if isinstance(shape, str) else int(shape)
    return _call(_lib.tdbg_datamatrix_encode, b, len(b), s)


def aztec(data, ecc=23, layers=0, encoding="utf-8"):
    """Aztec Code. ecc: percent (5-95); layers: 0=auto, 1..4 compact, -1..-32 full-range."""
    b = _as_bytes(data, encoding)
    return _call(_lib.tdbg_aztec_encode, b, len(b), int(ecc), int(layers))


def maxicode(data, mode=4, postcode=None, country=0, service=0, encoding="latin-1"):
    """MaxiCode. mode 2/3 need postcode, country and service class; 4/5/6 take only data."""
    b = _as_bytes(data, encoding)
    pc = postcode.encode("ascii") if isinstance(postcode, str) else postcode
    return _call(_lib.tdbg_maxicode_encode, int(mode), b, len(b), pc, int(country), int(service))
