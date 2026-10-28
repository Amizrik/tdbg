/* QR Code encoder (ISO/IEC 18004:2015), model 2, versions 1-40. */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* Error correction codewords per block, indexed [ecc][version]. */
static const signed char ECC_PER_BLOCK[4][41] = {
    {-1,  7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30}, /* L */
    {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28}, /* M */
    {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30}, /* Q */
    {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30}, /* H */
};

/* Number of error correction blocks, indexed [ecc][version]. */
static const signed char NUM_BLOCKS[4][41] = {
    {-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4,  4,  4,  4,  4,  6,  6,  6,  6,  7,  8,  8,  9,  9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},
    {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5,  5,  8,  9,  9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},
    {-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8,  8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},
    {-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81},
};

/* Format-information bits for each ECC level (L=01, M=00, Q=11, H=10). */
static const int ECC_FORMAT_BITS[4] = { 1, 0, 3, 2 };

enum { MODE_NUMERIC = 1, MODE_ALNUM = 2, MODE_BYTE = 4 };

static const char ALNUM_CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

static int alnum_value(unsigned char c)
{
    const char *p;
    if (c == 0) return -1;
    p = strchr(ALNUM_CHARS, c);
    return p ? (int)(p - ALNUM_CHARS) : -1;
}

/* Number of modules available for data+ECC (excludes function patterns). */
static int raw_data_modules(int ver)
{
    int r = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        int na = ver / 7 + 2;
        r -= (25 * na - 10) * na - 55;
        if (ver >= 7) r -= 36;
    }
    return r;
}

static int data_codewords(int ver, int ecc)
{
    return raw_data_modules(ver) / 8 - ECC_PER_BLOCK[ecc][ver] * NUM_BLOCKS[ecc][ver];
}

static int count_bits(int mode, int ver)
{
    int g = ver <= 9 ? 0 : ver <= 26 ? 1 : 2;
    switch (mode) {
    case MODE_NUMERIC: { static const int t[3] = { 10, 12, 14 }; return t[g]; }
    case MODE_ALNUM:   { static const int t[3] = { 9, 11, 13 };  return t[g]; }
    default:           { static const int t[3] = { 8, 16, 16 };  return t[g]; }
    }
}

static int payload_bits(int mode, int len)
{
    switch (mode) {
    case MODE_NUMERIC: return len / 3 * 10 + (len % 3 == 2 ? 7 : len % 3 == 1 ? 4 : 0);
    case MODE_ALNUM:   return len / 2 * 11 + (len % 2) * 6;
    default:           return len * 8;
    }
}

/* Alignment pattern centre coordinates; returns how many. */
static int alignment_positions(int ver, int *pos)
{
    int n, step, i, p;
    if (ver == 1) return 0;
    n = ver / 7 + 2;
    step = (ver * 8 + n * 3 + 5) / (n * 4 - 4) * 2;
    pos[0] = 6;
    for (i = n - 1, p = ver * 4 + 10; i >= 1; i--, p -= step) pos[i] = p;
    return n;
}

typedef struct {
    int size;
    unsigned char *m;     /* module colour */
    unsigned char *fn;    /* 1 = function module (not data) */
} qr_grid;

static void set_fn(qr_grid *g, int x, int y, int dark)
{
    g->m[y * g->size + x] = (unsigned char)dark;
    g->fn[y * g->size + x] = 1;
}

static void draw_finder(qr_grid *g, int cx, int cy)
{
    int dx, dy;
    for (dy = -4; dy <= 4; dy++)
        for (dx = -4; dx <= 4; dx++) {
            int x = cx + dx, y = cy + dy;
            int d = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
            if (x >= 0 && x < g->size && y >= 0 && y < g->size)
                set_fn(g, x, y, d != 2 && d != 4);
        }
}

static void draw_format(qr_grid *g, int ecc, int mask)
{
    int data = ECC_FORMAT_BITS[ecc] << 3 | mask, rem = data, bits, i, n = g->size;
    for (i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    bits = (data << 10 | rem) ^ 0x5412;
#define BIT(i) ((bits >> (i)) & 1)
    for (i = 0; i <= 5; i++) set_fn(g, 8, i, BIT(i));
    set_fn(g, 8, 7, BIT(6));
    set_fn(g, 8, 8, BIT(7));
    set_fn(g, 7, 8, BIT(8));
    for (i = 9; i < 15; i++) set_fn(g, 14 - i, 8, BIT(i));
    for (i = 0; i < 8; i++) set_fn(g, n - 1 - i, 8, BIT(i));
    for (i = 8; i < 15; i++) set_fn(g, 8, n - 15 + i, BIT(i));
#undef BIT
    set_fn(g, 8, n - 8, 1); /* always-dark module */
}

static void draw_function_patterns(qr_grid *g, int ver)
{
    int i, j, n = g->size, pos[7], na;
    for (i = 0; i < n; i++) {
        set_fn(g, 6, i, i % 2 == 0);
        set_fn(g, i, 6, i % 2 == 0);
    }
    draw_finder(g, 3, 3);
    draw_finder(g, n - 4, 3);
    draw_finder(g, 3, n - 4);

    na = alignment_positions(ver, pos);
    for (i = 0; i < na; i++)
        for (j = 0; j < na; j++) {
            int dx, dy;
            if ((i == 0 && j == 0) || (i == 0 && j == na - 1) || (i == na - 1 && j == 0))
                continue;
            for (dy = -2; dy <= 2; dy++)
                for (dx = -2; dx <= 2; dx++) {
                    int d = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
                    set_fn(g, pos[i] + dx, pos[j] + dy, d != 1);
                }
        }

    draw_format(g, 0, 0); /* reserve the area; real bits drawn after masking */

    if (ver >= 7) {
        int rem = ver, bits;
        for (i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        bits = ver << 12 | rem;
        for (i = 0; i < 18; i++) {
            int bit = (bits >> i) & 1, a = n - 11 + i % 3, b = i / 3;
            set_fn(g, a, b, bit);
            set_fn(g, b, a, bit);
        }
    }
}

static void draw_codewords(qr_grid *g, const unsigned char *cw, int ncw)
{
    int i = 0, right, vert, j, n = g->size, total = ncw * 8;
    for (right = n - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (vert = 0; vert < n; vert++)
            for (j = 0; j < 2; j++) {
                int x = right - j;
                int upward = ((right + 1) & 2) == 0;
                int y = upward ? n - 1 - vert : vert;
                if (!g->fn[y * n + x] && i < total) {
                    g->m[y * n + x] = (unsigned char)((cw[i >> 3] >> (7 - (i & 7))) & 1);
                    i++;
                }
            }
    }
}

static int mask_bit(int mask, int x, int y)
{
    switch (mask) {
    case 0: return (x + y) % 2 == 0;
    case 1: return y % 2 == 0;
    case 2: return x % 3 == 0;
    case 3: return (x + y) % 3 == 0;
    case 4: return (x / 3 + y / 2) % 2 == 0;
    case 5: return x * y % 2 + x * y % 3 == 0;
    case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
    default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
    }
}

static void apply_mask(qr_grid *g, int mask)
{
    int x, y, n = g->size;
    for (y = 0; y < n; y++)
        for (x = 0; x < n; x++)
            if (!g->fn[y * n + x] && mask_bit(mask, x, y))
                g->m[y * n + x] ^= 1;
}

/* Penalty score per ISO 18004 section 7.8.3. */
static long penalty(const qr_grid *g)
{
    static const unsigned char F1[11] = { 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0 };
    static const unsigned char F2[11] = { 0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1 };
    long score = 0;
    int n = g->size, x, y, k, dark = 0, pass;

    for (pass = 0; pass < 2; pass++) /* 0 = rows, 1 = columns */
        for (y = 0; y < n; y++) {
            int run = 1;
            for (x = 0; x < n; x++) {
                int c = pass ? g->m[x * n + y] : g->m[y * n + x];
                if (x > 0) {
                    int p = pass ? g->m[(x - 1) * n + y] : g->m[y * n + x - 1];
                    if (c == p) run++;
                    else { if (run >= 5) score += run - 2; run = 1; }
                }
                if (x + 11 <= n) {
                    int m1 = 1, m2 = 1;
                    for (k = 0; k < 11; k++) {
                        int v = pass ? g->m[(x + k) * n + y] : g->m[y * n + x + k];
                        if (v != F1[k]) m1 = 0;
                        if (v != F2[k]) m2 = 0;
                    }
                    score += 40 * (m1 + m2);
                }
            }
            if (run >= 5) score += run - 2;
        }

    for (y = 0; y < n - 1; y++)
        for (x = 0; x < n - 1; x++) {
            int c = g->m[y * n + x];
            if (c == g->m[y * n + x + 1] && c == g->m[(y + 1) * n + x] && c == g->m[(y + 1) * n + x + 1])
                score += 3;
        }

    for (k = 0; k < n * n; k++) dark += g->m[k];
    {
        long pct = (long)dark * 100 / ((long)n * n);
        long dev = pct > 50 ? pct - 50 : 50 - pct;
        score += dev / 5 * 10;
    }
    return score;
}

int tdbg_qr_encode(const unsigned char *data, int len, int ecc, int min_ver,
                 int mask, tdbg_symbol **out)
{
    int mode, ver, i, e, nbits = 0, cap = 0, ndc, nblocks, ecclen, raw, nshort, shortlen;
    tdbg_bits bb;
    unsigned char *dcw = NULL, *final = NULL;
    int *blk = NULL, *eccbuf = NULL;
    qr_grid g = { 0, NULL, NULL };
    tdbg_symbol *sym;
    int rc = TDBG_OK;

    if (!out || (!data && len) || len < 0 || ecc < 0 || ecc > 3 ||
        min_ver < 0 || min_ver > 40 || mask < -1 || mask > 7)
        return TDBG_ERR_ARG;
    *out = NULL;

    /* choose the most compact single mode able to carry everything */
    mode = MODE_NUMERIC;
    for (i = 0; i < len; i++) {
        if (data[i] >= '0' && data[i] <= '9') continue;
        if (alnum_value(data[i]) >= 0) { if (mode == MODE_NUMERIC) mode = MODE_ALNUM; }
        else { mode = MODE_BYTE; break; }
    }

    for (ver = min_ver ? min_ver : 1; ver <= 40; ver++) {
        int cb = count_bits(mode, ver);
        if (len >= (1 << cb)) continue;
        nbits = 4 + cb + payload_bits(mode, len);
        cap = data_codewords(ver, ecc) * 8;
        if (nbits <= cap) break;
    }
    if (ver > 40) return TDBG_ERR_TOO_LONG;

    /* use a stronger ECC level if it still fits in the same version */
    for (e = ecc + 1; e <= 3; e++)
        if (nbits <= data_codewords(ver, e) * 8) ecc = e;
    cap = data_codewords(ver, ecc) * 8;

    /* ---- build the data bit stream */
    tdbg_bits_init(&bb);
    tdbg_bits_put(&bb, (unsigned)mode, 4);
    tdbg_bits_put(&bb, (unsigned)len, count_bits(mode, ver));
    if (mode == MODE_NUMERIC) {
        for (i = 0; i + 3 <= len; i += 3)
            tdbg_bits_put(&bb, (unsigned)((data[i] - '0') * 100 + (data[i + 1] - '0') * 10 + data[i + 2] - '0'), 10);
        if (len - i == 2) tdbg_bits_put(&bb, (unsigned)((data[i] - '0') * 10 + data[i + 1] - '0'), 7);
        else if (len - i == 1) tdbg_bits_put(&bb, (unsigned)(data[i] - '0'), 4);
    } else if (mode == MODE_ALNUM) {
        for (i = 0; i + 2 <= len; i += 2)
            tdbg_bits_put(&bb, (unsigned)(alnum_value(data[i]) * 45 + alnum_value(data[i + 1])), 11);
        if (i < len) tdbg_bits_put(&bb, (unsigned)alnum_value(data[i]), 6);
    } else {
        for (i = 0; i < len; i++) tdbg_bits_put(&bb, data[i], 8);
    }
    tdbg_bits_put(&bb, 0, cap - bb.len < 4 ? cap - bb.len : 4); /* terminator */
    tdbg_bits_put(&bb, 0, (8 - bb.len % 8) % 8);
    for (i = 0xEC; bb.len < cap; i ^= 0xEC ^ 0x11) tdbg_bits_put(&bb, (unsigned)i, 8);
    if (bb.oom) { rc = TDBG_ERR_NOMEM; goto done; }

    /* ---- split into blocks, add ECC, interleave */
    ndc = cap / 8;
    nblocks = NUM_BLOCKS[ecc][ver];
    ecclen = ECC_PER_BLOCK[ecc][ver];
    raw = raw_data_modules(ver) / 8;
    nshort = nblocks - raw % nblocks;
    shortlen = raw / nblocks;           /* codewords in a short block incl. ECC */

    dcw = (unsigned char *)malloc((size_t)ndc);
    final = (unsigned char *)malloc((size_t)raw);
    blk = (int *)malloc(sizeof(int) * (size_t)(shortlen + 1));
    eccbuf = (int *)malloc(sizeof(int) * (size_t)nblocks * (size_t)ecclen);
    if (!dcw || !final || !blk || !eccbuf) { rc = TDBG_ERR_NOMEM; goto done; }
    for (i = 0; i < ndc; i++) dcw[i] = (unsigned char)tdbg_bits_get(&bb, i * 8, 8);

    {
        int b, k, off = 0, idx = 0, maxdata = shortlen + 1 - ecclen;
        for (b = 0; b < nblocks; b++) {
            int dl = shortlen - ecclen + (b >= nshort);
            for (k = 0; k < dl; k++) blk[k] = dcw[off + k];
            rc = tdbg_rs_encode(8, 0x11D, 0, blk, dl, eccbuf + b * ecclen, ecclen);
            if (rc) goto done;
            off += dl;
        }
        /* interleave data codewords */
        for (k = 0; k < maxdata; k++) {
            off = 0;
            for (b = 0; b < nblocks; b++) {
                int dl = shortlen - ecclen + (b >= nshort);
                if (k < dl) final[idx++] = dcw[off + k];
                off += dl;
            }
        }
        for (k = 0; k < ecclen; k++)
            for (b = 0; b < nblocks; b++) final[idx++] = (unsigned char)eccbuf[b * ecclen + k];
    }

    /* ---- place modules */
    g.size = ver * 4 + 17;
    g.m = (unsigned char *)calloc((size_t)g.size * (size_t)g.size, 1);
    g.fn = (unsigned char *)calloc((size_t)g.size * (size_t)g.size, 1);
    if (!g.m || !g.fn) { rc = TDBG_ERR_NOMEM; goto done; }
    draw_function_patterns(&g, ver);
    draw_codewords(&g, final, raw);

    if (mask < 0) {
        long best = -1;
        int m;
        for (m = 0; m < 8; m++) {
            long p;
            apply_mask(&g, m);
            draw_format(&g, ecc, m);
            p = penalty(&g);
            if (best < 0 || p < best) { best = p; mask = m; }
            apply_mask(&g, m); /* undo (XOR) */
        }
    }
    apply_mask(&g, mask);
    draw_format(&g, ecc, mask);

    sym = tdbg_symbol_new(g.size, g.size, TDBG_GRID_SQUARE, 4);
    if (!sym) { rc = TDBG_ERR_NOMEM; goto done; }
    memcpy(sym->modules, g.m, (size_t)g.size * (size_t)g.size);
    *out = sym;

done:
    tdbg_bits_free(&bb);
    free(dcw); free(final); free(blk); free(eccbuf);
    free(g.m); free(g.fn);
    return rc;
}
