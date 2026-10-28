"""Round-trip tests: encode with tdbg, decode with an independent reader (zxing-cpp).

    pip install zxing-cpp pillow
    PYTHONPATH=python python tests/test_tdbg.py

Without zxing-cpp only structural checks run.
"""
import random
import sys
import xml.dom.minidom

import tdbg

try:
    import zxingcpp
except ImportError:
    zxingcpp = None

random.seed(2026)
FAILS = []
COUNT = 0


def decode(sym, fmt):
    img = sym.to_pil(scale=4 if not sym.is_hex else 8)
    res = zxingcpp.read_barcodes(img, formats=getattr(zxingcpp.BarcodeFormat, fmt))
    return res[0] if res else None


def check(name, sym, fmt, expect):
    global COUNT
    COUNT += 1
    # structural: SVG is well-formed, modules are 0/1
    xml.dom.minidom.parseString(sym.to_svg())
    if zxingcpp is None:
        return
    r = decode(sym, fmt)
    got = r.bytes if r else None
    ok = got == expect if isinstance(expect, bytes) else (got is not None and expect(r))
    if not ok:
        FAILS.append((name, fmt, repr(sym), (got or b"")[:60]))


def rand_text(alphabet, n):
    return bytes(random.choice(alphabet) for _ in range(n))


DIGITS = b"0123456789"
ALNUM = b"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 $%*+-./:"
PRINTABLE = bytes(range(32, 127))
ANY = bytes(range(256))

samples = [b"12345", b"HELLO WORLD", b"Hello, World!", "中文测试 ✓".encode(),
           b"https://example.com/path?q=1&r=2"]
for n in (1, 7, 40, 150, 600):
    for alpha in (DIGITS, ALNUM, PRINTABLE, ANY):
        samples.append(rand_text(alpha, n))

# QR: every ECC level, a forced version and every mask
for d in samples:
    for ecc in "LMQH":
        try:
            check("qr", tdbg.qr(d, ecc=ecc), "QRCode", d)
        except tdbg.BarcodeError:
            pass
for m in range(8):
    check("qr-mask", tdbg.qr(b"mask test", mask=m), "QRCode", b"mask test")
big = rand_text(PRINTABLE, 2900)
check("qr-v40", tdbg.qr(big, ecc="L"), "QRCode", big)
check("qr-v12", tdbg.qr(b"x", version=12), "QRCode", b"x")

# Data Matrix: square, rectangle, binary, the largest symbol
for d in samples:
    for shape in ("square", "rect"):
        try:
            check("dm-" + shape, tdbg.datamatrix(d, shape=shape), "DataMatrix", d)
        except tdbg.BarcodeError:
            pass
big = rand_text(DIGITS, 3000)
check("dm-144", tdbg.datamatrix(big), "DataMatrix", big)

# Aztec: compact and full range, several ECC levels, forced layers
for d in samples + [rand_text(ANY, 1500), rand_text(PRINTABLE, 3000)]:
    for ecc in (10, 23, 50):
        try:
            check("aztec", tdbg.aztec(d, ecc=ecc), "Aztec", d)
        except tdbg.BarcodeError:
            pass
check("aztec-compact4", tdbg.aztec(b"layers", layers=4), "Aztec", b"layers")
check("aztec-full10", tdbg.aztec(b"layers", layers=-10), "Aztec", b"layers")

# Every symbol size of every symbology
for v in range(1, 41):
    for ecc in "LH":
        d = b"V%02d" % v
        check("qr-ver%d" % v, tdbg.qr(d, ecc=ecc, version=v), "QRCode", d)
for cap in (3, 5, 8, 12, 18, 22, 30, 36, 44, 62, 86, 114, 144, 174, 204, 280, 368, 456, 576,
            696, 816, 1050, 1304, 1558, 10, 16, 32, 49):
    d = b"a" * cap  # one ASCII codeword per character -> fills exactly that size
    for shape in ("square", "rect"):
        try:
            check("dm-%d" % cap, tdbg.datamatrix(d, shape=shape), "DataMatrix", d)
        except tdbg.BarcodeError:
            pass
for layers in list(range(1, 5)) + [-l for l in range(4, 33)]:
    d = b"layers %d" % layers
    check("aztec-layers%d" % layers, tdbg.aztec(d, layers=layers), "Aztec", d)

# MaxiCode: modes 4/5/6 with text, Latin-1 and long digit runs
maxi_samples = [b"HELLO WORLD", b"Mixed Case text, with punctuation!?", b"123456789012345678901234",
                "\xc4\xd6\xdc \xe4\xf6\xfc \xdf \xa9\xae".encode("latin-1"),
                b"tab\there\x1d\x1e", rand_text(PRINTABLE, 60), rand_text(ANY, 30)]
for d in maxi_samples:
    for mode in (4, 5, 6):
        try:
            sym = tdbg.maxicode(d, mode=mode)
        except tdbg.BarcodeError:
            continue
        if mode == 6:
            check("maxi6", sym, "MaxiCode", lambda r: True)  # reader programming, content not standard text
        else:
            check("maxi%d" % mode, sym, "MaxiCode", d)

# Structured carrier message (modes 2 and 3)
sec = b"[)>\x1e01\x1d961Z00004951\x1dUPSN\x1d06X610\x1d159\x1d1234567\x1d1/1\x1d\x1dY\x1d\x1dUT\x1e\x04"
check("maxi2", tdbg.maxicode(sec, mode=2, postcode="152382802", country=840, service=1), "MaxiCode",
      lambda r: b"152382802" in r.bytes and b"840" in r.bytes and b"UPSN" in r.bytes)
check("maxi3", tdbg.maxicode(b"Ship it", mode=3, postcode="B1050", country=56, service=1), "MaxiCode",
      lambda r: b"B1050" in r.bytes and b"056" in r.bytes and b"Ship it" in r.bytes)

# Errors are reported, not crashed on
for fn, args in ((tdbg.qr, (b"x" * 8000,)), (tdbg.maxicode, (b"x" * 200,)),
                 (tdbg.datamatrix, (b"x" * 5000,)), (tdbg.aztec, (b"x" * 5000,))):
    try:
        fn(*args)
        FAILS.append(("too-long", fn.__name__, "no error raised", b""))
    except tdbg.BarcodeError as e:
        assert e.code == -2, e

print("tdbg %s: %d symbols checked%s" % (tdbg.library_version(), COUNT,
      "" if zxingcpp else " (structure only, zxing-cpp not installed)"))
for f in FAILS:
    print("FAIL", *f)
print("FAILED: %d" % len(FAILS) if FAILS else "all passed")
sys.exit(1 if FAILS else 0)
