"""MaxiCode orientation tests: the image is rotated by any angle, mirrored, or speckled with noise.

zxing-cpp cannot read these images, so this uses the test-side grid reader in maxicode_reader.py.

    PYTHONPATH=python python3 tests/test_maxicode_orientation.py

Needs numpy, scipy and Pillow; skipped if they are missing.
"""
import random
import sys

import tdbg

try:
    import numpy as np
    from PIL import Image, ImageOps
    from maxicode_reader import read
except ImportError:
    print("SKIP: numpy/scipy/pillow not installed")
    sys.exit(0)

random.seed(2026)
FAILS = []
COUNT = 0
SCALE = 10
QZ = 1


def expect(name, cond, detail=""):
    global COUNT
    COUNT += 1
    if not cond:
        FAILS.append(name)
        print("FAIL", name, detail)


def compare(sym, grid):
    """Mismatches between the read grid and the symbol, over the cells the reader sampled."""
    bad = 0
    for y, row in enumerate(grid):
        for x, dark in enumerate(row):
            if dark is not None and bool(sym[x, y]) != dark:
                bad += 1
    return bad


def render(sym):
    return sym.to_pil(scale=SCALE, quiet_zone=QZ).convert("L")


samples = [b"HELLO WORLD", b"Mixed Case text, with punctuation!?", b"x" * 60]
for data in samples:
    for mode in (4, 5):
        sym = tdbg.maxicode(data, mode=mode)
        img = render(sym)

        # upright baseline
        ang, grid = read(img, SCALE)
        expect("upright %s m%d" % (data[:8], mode), compare(sym, grid) == 0)

        # rotations: 90-degree steps, odd angles, and a few random ones
        for deg in (90, 180, 270, 13.5, 45, 137.25, 333.3, random.uniform(0, 360)):
            rot = img.rotate(deg, resample=Image.BICUBIC, expand=True, fillcolor=255)
            try:
                ang, grid = read(rot, SCALE)
                bad = compare(sym, grid)
            except ValueError as e:
                bad, ang = -1, str(e)
            expect("rot %.2f %s m%d" % (deg, data[:8], mode), bad == 0,
                   "mismatches=%s angle=%s" % (bad, ang))

        # mirror: MaxiCode is not mirror-symmetric, so the reader must not produce a valid read
        mir = ImageOps.mirror(img)
        try:
            ang, grid = read(mir, SCALE)
            bad = compare(sym, grid)
        except ValueError:
            bad = -1
        expect("mirror rejected %s m%d" % (data[:8], mode), bad != 0)

        # speckle noise: 2% of pixels inverted, plus a 10% rotated copy
        arr = np.asarray(img).copy()
        mask = np.random.default_rng(7).random(arr.shape) < 0.02
        noisy = Image.fromarray(np.where(mask, 255 - arr, arr).astype(np.uint8))
        noisy = noisy.rotate(29.0, resample=Image.BICUBIC, expand=True, fillcolor=255)
        try:
            ang, grid = read(noisy, SCALE)
            bad = compare(sym, grid)
        except ValueError as e:
            bad, ang = -1, str(e)
        expect("noise+rot %s m%d" % (data[:8], mode), bad == 0,
               "mismatches=%s angle=%s" % (bad, ang))

# modes 2 and 3 (structured carrier) share the layout and must also read at any angle
for mode, kw in ((2, {"postcode": "152382802", "country": 840, "service": 1}),
                 (3, {"postcode": "B1050", "country": 56, "service": 1})):
    sym = tdbg.maxicode(b"Ship it", mode=mode, **kw)
    rot = render(sym).rotate(61.0, resample=Image.BICUBIC, expand=True, fillcolor=255)
    ang, grid = read(rot, SCALE)
    expect("rot mode%d" % mode, compare(sym, grid) == 0)

print("%d checks, %d failed" % (COUNT, len(FAILS)))
if FAILS:
    sys.exit(1)
