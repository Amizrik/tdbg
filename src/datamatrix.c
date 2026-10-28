/* Data Matrix ECC 200 encoder (ISO/IEC 16022:2006). */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    int rows, cols;       /* full symbol size in modules */
    int rrows, rcols;     /* data region size (modules, excl. finder) */
    int ndata, necc;      /* codewords */
    int blocks;           /* interleaved RS blocks */
} dm_size;

static const dm_size DM_SIZES[] = {
    /* square */
    { 10, 10,  8,  8,    3,   5, 1 }, { 12, 12, 10, 10,    5,   7, 1 },
    { 14, 14, 12, 12,    8,  10, 1 }, { 16, 16, 14, 14,   12,  12, 1 },
    { 18, 18, 16, 16,   18,  14, 1 }, { 20, 20, 18, 18,   22,  18, 1 },
    { 22, 22, 20, 20,   30,  20, 1 }, { 24, 24, 22, 22,   36,  24, 1 },
    { 26, 26, 24, 24,   44,  28, 1 }, { 32, 32, 14, 14,   62,  36, 1 },
    { 36, 36, 16, 16,   86,  42, 1 }, { 40, 40, 18, 18,  114,  48, 1 },
    { 44, 44, 20, 20,  144,  56, 1 }, { 48, 48, 22, 22,  174,  68, 1 },
    { 52, 52, 24, 24,  204,  84, 2 }, { 64, 64, 14, 14,  280, 112, 2 },
    { 72, 72, 16, 16,  368, 144, 4 }, { 80, 80, 18, 18,  456, 192, 4 },
    { 88, 88, 20, 20,  576, 224, 4 }, { 96, 96, 22, 22,  696, 272, 4 },
    {104,104, 24, 24,  816, 336, 6 }, {120,120, 18, 18, 1050, 408, 6 },
    {132,132, 20, 20, 1304, 496, 8 }, {144,144, 22, 22, 1558, 620, 10 },
    /* rectangular */
    {  8, 18,  6, 16,    5,   7, 1 }, {  8, 32,  6, 14,   10,  11, 1 },
    { 12, 26, 10, 24,   16,  14, 1 }, { 12, 36, 10, 16,   22,  18, 1 },
    { 16, 36, 14, 16,   32,  24, 1 }, { 16, 48, 14, 22,   49,  28, 1 },
};
#define DM_NSQUARE 24
#define DM_NSIZES ((int)(sizeof DM_SIZES / sizeof DM_SIZES[0]))

/* ------------------------------------------------------------ encoding */

/* ASCII encodation: digit pairs packed, bytes >127 via Upper Shift. */
static int encode_ascii(const unsigned char *d, int len, unsigned char *cw)
{
    int i = 0, n = 0;
    while (i < len) {
        if (i + 1 < len && d[i] >= '0' && d[i] <= '9' && d[i + 1] >= '0' && d[i + 1] <= '9') {
            cw[n++] = (unsigned char)(130 + (d[i] - '0') * 10 + (d[i + 1] - '0'));
            i += 2;
        } else if (d[i] > 127) {
            cw[n++] = 235;                       /* Upper Shift */
            cw[n++] = (unsigned char)(d[i] - 127);
            i++;
        } else {
            cw[n++] = (unsigned char)(d[i] + 1);
            i++;
        }
    }
    return n;
}

static unsigned char rand255(int value, int pos)
{
    int r = ((149 * pos) % 255) + 1, t = value + r;
    return (unsigned char)(t <= 255 ? t : t - 256);
}

/* Base 256 encodation for the whole message (better for binary data). */
static int encode_base256(const unsigned char *d, int len, unsigned char *cw)
{
    int n = 0, i;
    cw[n++] = 231;                              /* latch to Base 256 */
    if (len <= 249) {
        cw[n] = rand255(len, n + 1); n++;
    } else {
        cw[n] = rand255(len / 250 + 249, n + 1); n++;
        cw[n] = rand255(len % 250, n + 1); n++;
    }
    for (i = 0; i < len; i++) { cw[n] = rand255(d[i], n + 1); n++; }
    return n;
}

/* ----------------------------------------------- module placement (Annex F) */

typedef struct { int nrow, ncol; int *a; } dm_place;

static void pl_module(dm_place *p, int row, int col, int chr, int bit)
{
    if (row < 0) { row += p->nrow; col += 4 - ((p->nrow + 4) % 8); }
    if (col < 0) { col += p->ncol; row += 4 - ((p->ncol + 4) % 8); }
    p->a[row * p->ncol + col] = 10 * chr + bit;
}

static void pl_utah(dm_place *p, int row, int col, int chr)
{
    pl_module(p, row - 2, col - 2, chr, 1);
    pl_module(p, row - 2, col - 1, chr, 2);
    pl_module(p, row - 1, col - 2, chr, 3);
    pl_module(p, row - 1, col - 1, chr, 4);
    pl_module(p, row - 1, col,     chr, 5);
    pl_module(p, row,     col - 2, chr, 6);
    pl_module(p, row,     col - 1, chr, 7);
    pl_module(p, row,     col,     chr, 8);
}

static void pl_corner1(dm_place *p, int chr)
{
    int R = p->nrow, C = p->ncol;
    pl_module(p, R - 1, 0, chr, 1); pl_module(p, R - 1, 1, chr, 2);
    pl_module(p, R - 1, 2, chr, 3); pl_module(p, 0, C - 2, chr, 4);
    pl_module(p, 0, C - 1, chr, 5); pl_module(p, 1, C - 1, chr, 6);
    pl_module(p, 2, C - 1, chr, 7); pl_module(p, 3, C - 1, chr, 8);
}

static void pl_corner2(dm_place *p, int chr)
{
    int R = p->nrow, C = p->ncol;
    pl_module(p, R - 3, 0, chr, 1); pl_module(p, R - 2, 0, chr, 2);
    pl_module(p, R - 1, 0, chr, 3); pl_module(p, 0, C - 4, chr, 4);
    pl_module(p, 0, C - 3, chr, 5); pl_module(p, 0, C - 2, chr, 6);
    pl_module(p, 0, C - 1, chr, 7); pl_module(p, 1, C - 1, chr, 8);
}

static void pl_corner3(dm_place *p, int chr)
{
    int R = p->nrow, C = p->ncol;
    pl_module(p, R - 3, 0, chr, 1); pl_module(p, R - 2, 0, chr, 2);
    pl_module(p, R - 1, 0, chr, 3); pl_module(p, 0, C - 2, chr, 4);
    pl_module(p, 0, C - 1, chr, 5); pl_module(p, 1, C - 1, chr, 6);
    pl_module(p, 2, C - 1, chr, 7); pl_module(p, 3, C - 1, chr, 8);
}

static void pl_corner4(dm_place *p, int chr)
{
    int R = p->nrow, C = p->ncol;
    pl_module(p, R - 1, 0, chr, 1);     pl_module(p, R - 1, C - 1, chr, 2);
    pl_module(p, 0, C - 3, chr, 3);     pl_module(p, 0, C - 2, chr, 4);
    pl_module(p, 0, C - 1, chr, 5);     pl_module(p, 1, C - 3, chr, 6);
    pl_module(p, 1, C - 2, chr, 7);     pl_module(p, 1, C - 1, chr, 8);
}

/* Fills p->a with 10*codeword+bit (codeword 1-based, bit 1 = MSB), or 0/1 for fixed modules. */
static void pl_run(dm_place *p)
{
    int R = p->nrow, C = p->ncol, chr = 1, row = 4, col = 0;
    do {
        if (row == R && col == 0) pl_corner1(p, chr++);
        if (row == R - 2 && col == 0 && C % 4) pl_corner2(p, chr++);
        if (row == R - 2 && col == 0 && C % 8 == 4) pl_corner3(p, chr++);
        if (row == R + 4 && col == 2 && !(C % 8)) pl_corner4(p, chr++);
        do {
            if (row < R && col >= 0 && !p->a[row * C + col]) pl_utah(p, row, col, chr++);
            row -= 2; col += 2;
        } while (row >= 0 && col < C);
        row += 1; col += 3;
        do {
            if (row >= 0 && col < C && !p->a[row * C + col]) pl_utah(p, row, col, chr++);
            row += 2; col -= 2;
        } while (row < R && col >= 0);
        row += 3; col += 1;
    } while (row < R || col < C);
    if (!p->a[R * C - 1]) p->a[R * C - 1] = p->a[R * C - C - 2] = 1;
}

/* ------------------------------------------------------------------ API */

int tdbg_datamatrix_encode(const unsigned char *data, int len, int shape, tdbg_symbol **out)
{
    unsigned char *cw = NULL;
    int ncw, i, b, si = -1, rc = TDBG_OK, first, last;
    const dm_size *sz;
    int *blkd = NULL, *blke = NULL;
    dm_place pl = { 0, 0, NULL };
    tdbg_symbol *sym;

    if (!out || (!data && len) || len < 0 || shape < 0 || shape > 2) return TDBG_ERR_ARG;
    *out = NULL;
    if (len > 3116) return TDBG_ERR_TOO_LONG;

    cw = (unsigned char *)malloc((size_t)len * 2 + 4);
    if (!cw) return TDBG_ERR_NOMEM;
    {
        int hi = 0;
        for (i = 0; i < len; i++) hi += data[i] > 127;
        ncw = encode_ascii(data, len, cw);
        if (hi * 3 > len && len + 3 < ncw) ncw = encode_base256(data, len, cw);
    }

    first = shape == TDBG_DM_RECT ? DM_NSQUARE : 0;
    last = shape == TDBG_DM_SQUARE ? DM_NSQUARE : DM_NSIZES;
    for (i = first; i < last; i++)
        if (DM_SIZES[i].ndata >= ncw && (si < 0 || DM_SIZES[i].ndata < DM_SIZES[si].ndata))
            si = i;
    if (si < 0) { free(cw); return TDBG_ERR_TOO_LONG; }
    sz = &DM_SIZES[si];

    {
        unsigned char *full = (unsigned char *)realloc(cw, (size_t)(sz->ndata + sz->necc));
        if (!full) { free(cw); return TDBG_ERR_NOMEM; }
        cw = full;
    }
    /* padding: 129 then 253-state randomised pads */
    for (i = ncw; i < sz->ndata; i++) {
        if (i == ncw) cw[i] = 129;
        else {
            int v = 129 + ((149 * (i + 1)) % 253) + 1;
            cw[i] = (unsigned char)(v <= 254 ? v : v - 254);
        }
    }

    /* Reed-Solomon, GF(256) poly 0x12D, roots a^1..a^n, interleaved blocks */
    {
        int bl = sz->ndata / sz->blocks + 1, el = sz->necc / sz->blocks;
        blkd = (int *)malloc(sizeof(int) * (size_t)bl);
        blke = (int *)malloc(sizeof(int) * (size_t)el);
        if (!blkd || !blke) { rc = TDBG_ERR_NOMEM; goto done; }
        for (b = 0; b < sz->blocks; b++) {
            int n = 0;
            for (i = b; i < sz->ndata; i += sz->blocks) blkd[n++] = cw[i];
            rc = tdbg_rs_encode(8, 0x12D, 1, blkd, n, blke, el);
            if (rc) goto done;
            for (i = 0; i < el; i++) cw[sz->ndata + i * sz->blocks + b] = (unsigned char)blke[i];
        }
    }

    /* place codewords into the mapping matrix */
    {
        int hreg = sz->rows / (sz->rrows + 2), vreg = sz->cols / (sz->rcols + 2);
        int r, c;
        pl.nrow = sz->rrows * hreg;
        pl.ncol = sz->rcols * vreg;
        pl.a = (int *)calloc((size_t)pl.nrow * (size_t)pl.ncol, sizeof(int));
        sym = tdbg_symbol_new(sz->cols, sz->rows, TDBG_GRID_SQUARE, 1);
        if (!pl.a || !sym) { tdbg_symbol_free(sym); rc = TDBG_ERR_NOMEM; goto done; }
        pl_run(&pl);

        /* finder pattern and clock tracks around every data region */
        for (r = 0; r < hreg; r++)
            for (c = 0; c < vreg; c++) {
                int y0 = r * (sz->rrows + 2), x0 = c * (sz->rcols + 2), k;
                int h = sz->rrows + 2, w = sz->rcols + 2;
                for (k = 0; k < w; k++) {
                    TDBG_SET(sym, x0 + k, y0 + h - 1, 1);       /* solid bottom */
                    TDBG_SET(sym, x0 + k, y0, (k & 1) == 0);    /* dotted top */
                }
                for (k = 0; k < h; k++) {
                    TDBG_SET(sym, x0, y0 + k, 1);               /* solid left */
                    TDBG_SET(sym, x0 + w - 1, y0 + k, (k & 1) == 1 || k == h - 1); /* dotted right */
                }
            }
        for (r = 0; r < pl.nrow; r++)
            for (c = 0; c < pl.ncol; c++) {
                int v = pl.a[r * pl.ncol + c], dark;
                if (v >= 10) dark = (cw[v / 10 - 1] >> (8 - v % 10)) & 1;
                else dark = v;
                TDBG_SET(sym, (c / sz->rcols) * (sz->rcols + 2) + 1 + c % sz->rcols,
                            (r / sz->rrows) * (sz->rrows + 2) + 1 + r % sz->rrows, dark);
            }
        *out = sym;
    }

done:
    free(cw); free(blkd); free(blke); free(pl.a);
    return rc;
}
