# 2D Barcodes Generator (tdbg)

[中文版](README.zh-CN.md)

A QR Code, Data Matrix, Aztec and MaxiCode generator written from scratch in pure C99. It has no third-party dependencies and provides a C API, a command-line tool, and a Python binding (ctypes).

## Layout

```
include/tdbg.h        Public API (the only header needed for FFI)
src/common.c          Bit buffer, generic GF(2^m) Reed-Solomon, symbol object, SVG output
src/qr.c              QR Code: versions 1-40, L/M/Q/H, numeric/alphanumeric/byte modes, automatic masking
src/datamatrix.c      Data Matrix ECC200: 24 square + 6 rectangular sizes, ASCII/Base256 encoding
src/aztec.c           Aztec: compact 1-4 layers, full-range 4-32 layers, 5 character modes + binary shift
src/maxicode.c        MaxiCode: modes 2/3 (structured carrier data), 4, 5, 6; code sets A-E + numeric compression
cli/main.c            Command-line tool
python/tdbg/          Python package (ctypes loads the C library; PNG is rendered in pure Python, SVG comes from C)
tests/test_tdbg.py    Round-trip tests decoded by an independent decoder (zxing-cpp)
tests/test_maxicode_orientation.py  MaxiCode orientation, mirroring and noise tests (uses maxicode_reader.py)
```

## Build

```bash
make            # build/libtdbg.{so,dylib} + build/libtdbg.a + build/tdbg
```

Or with CMake (also works on Windows/MSVC):

```bash
cmake -S . -B build && cmake --build build
```

## Python

```bash
pip install .
```

```python
import tdbg

tdbg.qr("https://example.com", ecc="M").save("qr.png", scale=8)
tdbg.datamatrix("ABC-123", shape="rect").save("dm.svg")
tdbg.aztec("Aztec 码", ecc=33).save("aztec.png")
tdbg.maxicode("HELLO", mode=4).save("maxi.png", scale=10)

# UPS-style structured MaxiCode (mode 2: numeric postcode)
tdbg.maxicode(secondary_bytes, mode=2, postcode="152382802", country=840, service=1)

sym = tdbg.qr("hi")
sym.width, sym.height, sym[0, 0]   # direct access to the module matrix
print(sym.to_text())               # terminal preview
```

Without installing: `make python` copies the shared library into `python/tdbg/`, then run `PYTHONPATH=python python3 ...`. The environment variable `TDBG_LIBRARY` can also point to the library.

## Command line

```bash
./build/tdbg qr -e H "hello" -o qr.svg
./build/tdbg datamatrix -r "ABC123" -o dm.pbm
./build/tdbg aztec -e 50 "text"
./build/tdbg maxicode -m 2 -p 152382802 -c 840 -k 1 "secondary message" -o maxi.svg
```

## C API

```c
tdbg_symbol *s;
int rc = tdbg_qr_encode((const unsigned char *)"hello", 5, TDBG_QR_ECC_M, 0, -1, &s);
if (rc == TDBG_OK) {
    /* s->modules[y * s->width + x] == 1 means a dark module */
    char *svg = tdbg_svg(s, 4.0, -1);
    tdbg_free(svg);
    tdbg_symbol_free(s);
} else {
    puts(tdbg_strerror(rc));
}
```

When `tdbg_symbol.grid` is `TDBG_GRID_HEX` (MaxiCode), odd rows are shifted half a module to the right, the row pitch is √3/2, and a bullseye finder pattern is drawn at the centre. `tdbg_svg()` and the Python PNG renderer both handle this.

## Tests

```bash
pip install zxing-cpp pillow
make test
```

The tests generate over 400 symbols (every size of every format, every error-correction level, binary and non-ASCII data), decode them with zxing-cpp, and compare the results byte for byte.

## Notes

- All encoding algorithms (Reed-Solomon, data encoding, mask scoring, Data Matrix module placement, the Aztec spiral layout, and so on) are implemented from scratch. The one external data table is MaxiCode's 33×30 module-position table. It is defined in figure 5 of ISO/IEC 16023 and cannot be derived by an algorithm, so it is transcribed from ZXing (Apache 2.0); the attribution is noted in the source.
- The encoder currently picks modes greedily. Its output is always valid, but not always the smallest possible symbol. QR Kanji mode, Data Matrix C40/Text/EDIFACT modes, ECI and structured append are not implemented yet.
