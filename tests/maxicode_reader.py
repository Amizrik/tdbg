"""Test-side MaxiCode grid reader: finds the bullseye and the orientation, then samples the modules.

zxing-cpp only reads upright, pure images, so it cannot check rotated symbols. This reader
works from the image alone: it locates the bullseye by correlation with the ISO/IEC 16023
ring pattern (rotation invariant), searches the rotation angle with the orientation modules,
and samples the 33x30 grid. A mirrored symbol matches no rotation and is rejected.

It checks the module layout and orientation, not the code words. Use the text round trip
in test_tdbg.py for that.

Needs numpy and scipy (tests only).
"""
import math
import os
import re

import numpy as np
from scipy.signal import fftconvolve

import tdbg
from tdbg import _BULLSEYE, _HEX_PITCH

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
CX, CY = 14.5, 16 * _HEX_PITCH          # bullseye centre in module units (renderer frame)
MIN_ORIENT = 0.9                        # fraction of orientation modules that must match


def load_grid_table():
    """MAXI_GRID from src/maxicode.c, so the reader and the encoder share one layout table."""
    with open(os.path.join(ROOT, "src", "maxicode.c")) as f:
        text = f.read()
    block = text[text.index("MAXI_GRID[33][30]"):]
    block = block[:block.index("};")]
    rows = [[int(v) for v in r.split(",")] for r in re.findall(r"\{([^{}]*)\}", block)]
    assert len(rows) == 33 and all(len(r) == 30 for r in rows)
    return rows


GRID = load_grid_table()
ORIENT = [(x, y, GRID[y][x] == -2) for y in range(33) for x in range(30) if GRID[y][x] in (-1, -2)]


def module_centre(x, y):
    """Physical centre of module (x, y) in module units, same frame as tdbg's renderer."""
    return x + 0.5 + 0.5 * (y & 1), y * _HEX_PITCH


def _template(scale):
    """Signed bullseye template: +1 dark ring, -1 light ring, 0 outside the 4.5X disk."""
    r_out = _BULLSEYE[0]
    half = int(math.ceil(r_out * scale)) + 1
    yy, xx = np.mgrid[-half:half + 1, -half:half + 1] / scale
    r = np.hypot(xx, yy)
    rr = _BULLSEYE
    dark = ((r >= rr[1]) & (r < rr[0])) | ((r >= rr[3]) & (r < rr[2])) | ((r >= rr[5]) & (r < rr[4]))
    t = np.where(r < r_out, np.where(dark, 1.0, -1.0), 0.0)
    return t


def find_centre(gray, scale):
    """Pixel position (continuous coordinates) of the bullseye centre."""
    signed = 1.0 - 2.0 * gray / 255.0      # dark pixels positive
    corr = fftconvolve(signed, _template(scale), mode="same")
    py, px = np.unravel_index(np.argmax(corr), corr.shape)
    return px + 0.5, py + 0.5


def _sample(gray, px, py):
    """Dark fraction in a 3x3 window around a continuous pixel position; True if dark."""
    ix, iy = int(math.floor(px)), int(math.floor(py))
    win = gray[max(iy - 1, 0):iy + 2, max(ix - 1, 0):ix + 2]
    return win.size > 0 and win.mean() < 128


def _to_pixel(cx, cy, scale, theta, u, v):
    du, dv = u - CX, v - CY
    c, s = math.cos(theta), math.sin(theta)
    return cx + scale * (c * du - s * dv), cy + scale * (s * du + c * dv)


def _bilinear(gray, px, py):
    """Grey level at a continuous pixel position; pixel i covers [i, i+1). Outside is white."""
    h, w = gray.shape
    x0, y0 = math.floor(px - 0.5), math.floor(py - 0.5)
    fx, fy = px - 0.5 - x0, py - 0.5 - y0
    def g(ix, iy):
        return gray[iy, ix] if 0 <= ix < w and 0 <= iy < h else 255.0
    top = g(x0, y0) * (1 - fx) + g(x0 + 1, y0) * fx
    bot = g(x0, y0 + 1) * (1 - fx) + g(x0 + 1, y0 + 1) * fx
    return top * (1 - fy) + bot * fy


def orientation_score(gray, cx, cy, scale, theta):
    """Mean agreement of the orientation modules, 1.0 when every module is exactly right."""
    total = 0.0
    for x, y, dark in ORIENT:
        u, v = module_centre(x, y)
        px, py = _to_pixel(cx, cy, scale, theta, u, v)
        level = _bilinear(gray, px, py) / 255.0
        total += (1.0 - level) if dark else level
    return total / len(ORIENT)


def read(img, scale):
    """Read a MaxiCode symbol from a Pillow image rendered at `scale` pixels per module.

    Returns (angle_degrees, grid) with grid[y][x] = True for dark modules (None on the
    unused and bullseye cells). Raises ValueError if no rotation matches the orientation pattern.
    """
    gray = np.asarray(img.convert("L"), dtype=float)
    cx, cy = find_centre(gray, scale)

    # coarse search, then refine around every near-best coarse angle
    coarse = [(orientation_score(gray, cx, cy, scale, math.radians(d)), d)
              for d in np.arange(0, 360, 0.5)]
    best = max(coarse)
    for sc0, d0 in coarse:
        if sc0 < best[0] - 0.05:
            continue
        for d in np.arange(d0 - 0.5, d0 + 0.5001, 0.02):
            sc = orientation_score(gray, cx, cy, scale, math.radians(d))
            if sc > best[0]:
                best = (sc, d)
    if best[0] < MIN_ORIENT:
        raise ValueError("no orientation pattern found (best %.2f)" % best[0])

    theta = math.radians(best[1])
    grid = [[None] * 30 for _ in range(33)]
    for y in range(33):
        for x in range(30):
            if GRID[y][x] == -3:
                continue
            u, v = module_centre(x, y)
            px, py = _to_pixel(cx, cy, scale, theta, u, v)
            grid[y][x] = _sample(gray, px, py)
    return best[1], grid
