/* Aztec Code encoder (ISO/IEC 24778:2008), compact and full-range symbols. */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

enum { M_UPPER, M_LOWER, M_MIXED, M_PUNCT, M_DIGIT, M_COUNT };

/* code of byte c in each mode, -1 if absent */
static int char_code[M_COUNT][256];

/* Latch sequences between modes: up to 3 (code, bits) pairs. */
typedef struct { int n; int code[3]; int bits[3]; } latch_seq;
static latch_seq latch[M_COUNT][M_COUNT];

static int tables_ready;

static void set_latch(int from, int to, int n, int c0, int b0, int c1, int b1, int c2, int b2)
{
    latch_seq *l = &latch[from][to];
    l->n = n;
    l->code[0] = c0; l->bits[0] = b0;
    l->code[1] = c1; l->bits[1] = b1;
    l->code[2] = c2; l->bits[2] = b2;
}

static void init_tables(void)
{
    static const unsigned char mixed[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 27, 28, 29, 30, 31,
        '@', '\\', '^', '_', '`', '|', '~', 127 };
    static const char punct[] = "!\"#$%&'()*+,-./:;<=>?[]{}";
    int m, c, i;
    if (tables_ready) return;
    for (m = 0; m < M_COUNT; m++)
        for (c = 0; c < 256; c++) char_code[m][c] = -1;

    char_code[M_UPPER][' '] = char_code[M_LOWER][' '] = 1;
    char_code[M_MIXED][' '] = char_code[M_DIGIT][' '] = 1;
    for (c = 'A'; c <= 'Z'; c++) char_code[M_UPPER][c] = c - 'A' + 2;
    for (c = 'a'; c <= 'z'; c++) char_code[M_LOWER][c] = c - 'a' + 2;
    for (c = '0'; c <= '9'; c++) char_code[M_DIGIT][c] = c - '0' + 2;
    char_code[M_DIGIT][','] = 12;
    char_code[M_DIGIT]['.'] = 13;
    for (i = 0; i < (int)sizeof mixed; i++) char_code[M_MIXED][mixed[i]] = i + 2;
    char_code[M_PUNCT]['\r'] = 1;
    for (i = 0; punct[i]; i++) char_code[M_PUNCT][(unsigned char)punct[i]] = i + 6;

    /* codes: U/L 29(M),31(P),14(D); L/L 28; M/L 29; P/L 30(M); D/L 30 */
    memset(latch, 0, sizeof latch);
    set_latch(M_UPPER, M_LOWER, 1, 28, 5, 0, 0, 0, 0);
    set_latch(M_UPPER, M_MIXED, 1, 29, 5, 0, 0, 0, 0);
    set_latch(M_UPPER, M_DIGIT, 1, 30, 5, 0, 0, 0, 0);
    set_latch(M_UPPER, M_PUNCT, 2, 29, 5, 30, 5, 0, 0);
    set_latch(M_LOWER, M_UPPER, 2, 30, 5, 14, 4, 0, 0);
    set_latch(M_LOWER, M_MIXED, 1, 29, 5, 0, 0, 0, 0);
    set_latch(M_LOWER, M_DIGIT, 1, 30, 5, 0, 0, 0, 0);
    set_latch(M_LOWER, M_PUNCT, 2, 29, 5, 30, 5, 0, 0);
    set_latch(M_MIXED, M_UPPER, 1, 29, 5, 0, 0, 0, 0);
    set_latch(M_MIXED, M_LOWER, 1, 28, 5, 0, 0, 0, 0);
    set_latch(M_MIXED, M_DIGIT, 2, 29, 5, 30, 5, 0, 0);
    set_latch(M_MIXED, M_PUNCT, 1, 30, 5, 0, 0, 0, 0);
    set_latch(M_PUNCT, M_UPPER, 1, 31, 5, 0, 0, 0, 0);
    set_latch(M_PUNCT, M_LOWER, 2, 31, 5, 28, 5, 0, 0);
    set_latch(M_PUNCT, M_MIXED, 2, 31, 5, 29, 5, 0, 0);
    set_latch(M_PUNCT, M_DIGIT, 2, 31, 5, 30, 5, 0, 0);
    set_latch(M_DIGIT, M_UPPER, 1, 14, 4, 0, 0, 0, 0);
    set_latch(M_DIGIT, M_LOWER, 2, 14, 4, 28, 5, 0, 0);
    set_latch(M_DIGIT, M_MIXED, 2, 14, 4, 29, 5, 0, 0);
    set_latch(M_DIGIT, M_PUNCT, 3, 14, 4, 29, 5, 30, 5);
    tables_ready = 1;
}

static int mode_bits(int m) { return m == M_DIGIT ? 4 : 5; }

static void emit_latch(tdbg_bits *b, int from, int to)
{
    int i;
    for (i = 0; i < latch[from][to].n; i++)
        tdbg_bits_put(b, (unsigned)latch[from][to].code[i], latch[from][to].bits[i]);
}

/* Which mode holds byte c, preferring `cur`; -1 if it needs binary shift. */
static int find_mode(int cur, unsigned char c)
{
    static const int order[M_COUNT] = { M_UPPER, M_LOWER, M_DIGIT, M_MIXED, M_PUNCT };
    int i;
    if (char_code[cur][c] >= 0) return cur;
    for (i = 0; i < M_COUNT; i++)
        if (char_code[order[i]][c] >= 0) return order[i];
    return -1;
}

/* Greedy high-level encoder: latches for runs, shifts for single characters. */
static void encode_text(const unsigned char *d, int len, tdbg_bits *b)
{
    int mode = M_UPPER, i = 0;
    while (i < len) {
        unsigned char c = d[i];
        int target = find_mode(mode, c);

        if (target == mode) {
            tdbg_bits_put(b, (unsigned)char_code[mode][c], mode_bits(mode));
            i++;
            continue;
        }
        if (target < 0) {
            /* binary shift run; B/S only exists in Upper, Lower and Mixed */
            int n = 0;
            while (i + n < len && n < 2078 && find_mode(mode, d[i + n]) < 0) n++;
            if (mode == M_PUNCT || mode == M_DIGIT) {
                emit_latch(b, mode, M_UPPER);
                mode = M_UPPER;
            }
            tdbg_bits_put(b, 31, 5);
            if (n <= 31) tdbg_bits_put(b, (unsigned)n, 5);
            else { tdbg_bits_put(b, 0, 5); tdbg_bits_put(b, (unsigned)(n - 31), 11); }
            while (n--) tdbg_bits_put(b, d[i++], 8);
            continue;
        }
        {
            int next_same = i + 1 < len && char_code[target][d[i + 1]] >= 0 &&
                            char_code[mode][d[i + 1]] < 0;
            /* single-character shifts */
            if (!next_same && target == M_PUNCT && mode != M_PUNCT) {
                tdbg_bits_put(b, 0, mode_bits(mode));            /* P/S */
                tdbg_bits_put(b, (unsigned)char_code[M_PUNCT][c], 5);
                i++;
                continue;
            }
            if (!next_same && target == M_UPPER && (mode == M_LOWER || mode == M_DIGIT)) {
                tdbg_bits_put(b, mode == M_LOWER ? 28u : 15u, mode_bits(mode)); /* U/S */
                tdbg_bits_put(b, (unsigned)char_code[M_UPPER][c], 5);
                i++;
                continue;
            }
            emit_latch(b, mode, target);
            mode = target;
        }
    }
}

/* Split into wordsize-bit codewords, stuffing to avoid all-0 / all-1 words. */
static int stuff_bits(const tdbg_bits *in, int ws, int *words, int maxwords)
{
    int n = 0, i, j, mask = (1 << ws) - 2;
    for (i = 0; i < in->len; i += ws) {
        int w = 0;
        for (j = 0; j < ws; j++)
            if (i + j >= in->len || in->bits[i + j]) w |= 1 << (ws - 1 - j);
        if (n >= maxwords) return -1;
        if ((w & mask) == mask) { words[n++] = w & mask; i--; }
        else if ((w & mask) == 0) { words[n++] = w | 1; i--; }
        else words[n++] = w;
    }
    return n;
}

static int total_bits(int layers, int compact)
{
    return ((compact ? 88 : 112) + 16 * layers) * layers;
}

static int word_size(int layers)
{
    return layers <= 2 ? 6 : layers <= 8 ? 8 : layers <= 22 ? 10 : 12;
}

static int gf_poly(int ws)
{
    switch (ws) {
    case 4: return 0x13;
    case 6: return 0x43;
    case 8: return 0x12D;
    case 10: return 0x409;
    default: return 0x1069;
    }
}

static void bullseye(tdbg_symbol *s, int c, int size)
{
    int i, j;
    for (i = 0; i < size; i += 2)
        for (j = c - i; j <= c + i; j++) {
            TDBG_SET(s, j, c - i, 1); TDBG_SET(s, j, c + i, 1);
            TDBG_SET(s, c - i, j, 1); TDBG_SET(s, c + i, j, 1);
        }
    /* orientation marks */
    TDBG_SET(s, c - size, c - size, 1); TDBG_SET(s, c - size + 1, c - size, 1);
    TDBG_SET(s, c - size, c - size + 1, 1);
    TDBG_SET(s, c + size, c - size, 1); TDBG_SET(s, c + size, c - size + 1, 1);
    TDBG_SET(s, c + size, c + size - 1, 1);
}

int tdbg_aztec_encode(const unsigned char *data, int len, int ecc_pct, int req_layers, tdbg_symbol **out)
{
    tdbg_bits bits, msg;
    int *words = NULL, nwords = 0, layers = 0, compact = 0, ws = 0, tb = 0, rc = TDBG_OK;
    int i;
    tdbg_symbol *sym = NULL;
    int *amap = NULL;

    if (!out || (!data && len) || len < 0 || ecc_pct < 5 || ecc_pct > 95 ||
        req_layers < -32 || req_layers > 4)
        return TDBG_ERR_ARG;
    *out = NULL;
    init_tables();

    tdbg_bits_init(&bits);
    tdbg_bits_init(&msg);
    encode_text(data, len, &bits);
    if (bits.oom) { rc = TDBG_ERR_NOMEM; goto done; }

    words = (int *)malloc(sizeof(int) * 4096);
    if (!words) { rc = TDBG_ERR_NOMEM; goto done; }

    /* choose the symbol size: compact 1-4, then full-range 4-32
       (full-range 1-3 are never smaller than the compact equivalents) */
    for (i = 0; i <= 32; i++) {
        int c = i <= 3, l = c ? i + 1 : i, usable, eccbits;
        if (req_layers > 0) { if (i > 0) break; c = 1; l = req_layers; }
        if (req_layers < 0) { if (i > 0) break; c = 0; l = -req_layers; }
        tb = total_bits(l, c);
        ws = word_size(l);
        usable = tb - tb % ws;
        eccbits = bits.len * ecc_pct / 100 + 11;
        if (bits.len + eccbits > usable) continue;
        nwords = stuff_bits(&bits, ws, words, 4096);
        if (nwords == 0) words[nwords++] = (1 << ws) - 2;   /* empty message */
        if (nwords > (c ? 64 : 2048)) continue;
        if (nwords * ws + eccbits <= usable) {
            layers = l; compact = c;
            break;
        }
    }
    if (!layers) { rc = TDBG_ERR_TOO_LONG; goto done; }

    /* data words + check words, preceded by tb % ws zero padding bits */
    {
        int total = tb / ws, *ecc;
        ecc = (int *)malloc(sizeof(int) * (size_t)(total - nwords));
        if (!ecc) { rc = TDBG_ERR_NOMEM; goto done; }
        rc = tdbg_rs_encode(ws, gf_poly(ws), 1, words, nwords, ecc, total - nwords);
        if (rc) { free(ecc); goto done; }
        tdbg_bits_put(&msg, 0, tb % ws);
        for (i = 0; i < nwords; i++) tdbg_bits_put(&msg, (unsigned)words[i], ws);
        for (i = 0; i < total - nwords; i++) tdbg_bits_put(&msg, (unsigned)ecc[i], ws);
        free(ecc);
        if (msg.oom) { rc = TDBG_ERR_NOMEM; goto done; }
    }

    {
        int base = (compact ? 11 : 14) + layers * 4, size, center;
        amap = (int *)malloc(sizeof(int) * (size_t)base);
        if (!amap) { rc = TDBG_ERR_NOMEM; goto done; }
        if (compact) {
            size = base;
            for (i = 0; i < base; i++) amap[i] = i;
        } else {
            int oc = base / 2;
            size = base + 1 + 2 * ((base / 2 - 1) / 15);
            center = size / 2;
            for (i = 0; i < oc; i++) {
                int off = i + i / 15;
                amap[oc - i - 1] = center - off - 1;
                amap[oc + i] = center + off + 1;
            }
        }
        center = size / 2;
        sym = tdbg_symbol_new(size, size, TDBG_GRID_SQUARE, 1);
        if (!sym) { rc = TDBG_ERR_NOMEM; goto done; }

        /* data layers, spiralling inward->outward; each layer is 2 modules thick */
        {
            int off = 0, j, k;
#define MB(p) (msg.bits[p])
            for (i = 0; i < layers; i++) {
                int rs = (layers - i) * 4 + (compact ? 9 : 12);
                for (j = 0; j < rs; j++) {
                    int co = j * 2;
                    for (k = 0; k < 2; k++) {
                        if (MB(off + co + k)) TDBG_SET(sym, amap[i * 2 + k], amap[i * 2 + j], 1);
                        if (MB(off + rs * 2 + co + k)) TDBG_SET(sym, amap[i * 2 + j], amap[base - 1 - i * 2 - k], 1);
                        if (MB(off + rs * 4 + co + k)) TDBG_SET(sym, amap[base - 1 - i * 2 - k], amap[base - 1 - i * 2 - j], 1);
                        if (MB(off + rs * 6 + co + k)) TDBG_SET(sym, amap[base - 1 - i * 2 - j], amap[i * 2 + k], 1);
                    }
                }
                off += rs * 8;
            }
#undef MB
        }

        /* mode message: layer count + data word count, RS over GF(16) */
        {
            int mw[10], mlen, nd, m[40];
            tdbg_bits mm;
            tdbg_bits_init(&mm);
            if (compact) {
                tdbg_bits_put(&mm, (unsigned)(layers - 1), 2);
                tdbg_bits_put(&mm, (unsigned)(nwords - 1), 6);
                nd = 2; mlen = 7;
            } else {
                tdbg_bits_put(&mm, (unsigned)(layers - 1), 5);
                tdbg_bits_put(&mm, (unsigned)(nwords - 1), 11);
                nd = 4; mlen = 10;
            }
            for (i = 0; i < nd; i++) mw[i] = (int)tdbg_bits_get(&mm, i * 4, 4);
            tdbg_bits_free(&mm);
            rc = tdbg_rs_encode(4, 0x13, 1, mw, nd, mw + nd, mlen - nd);
            if (rc) goto done;
            for (i = 0; i < mlen * 4; i++) m[i] = (mw[i / 4] >> (3 - i % 4)) & 1;
            if (compact) {
                for (i = 0; i < 7; i++) {
                    int o = center - 3 + i;
                    if (m[i]) TDBG_SET(sym, o, center - 5, 1);
                    if (m[i + 7]) TDBG_SET(sym, center + 5, o, 1);
                    if (m[20 - i]) TDBG_SET(sym, o, center + 5, 1);
                    if (m[27 - i]) TDBG_SET(sym, center - 5, o, 1);
                }
            } else {
                for (i = 0; i < 10; i++) {
                    int o = center - 5 + i + i / 5;
                    if (m[i]) TDBG_SET(sym, o, center - 7, 1);
                    if (m[i + 10]) TDBG_SET(sym, center + 7, o, 1);
                    if (m[29 - i]) TDBG_SET(sym, o, center + 7, 1);
                    if (m[39 - i]) TDBG_SET(sym, center - 7, o, 1);
                }
            }
        }

        if (compact) bullseye(sym, center, 5);
        else {
            int j, k;
            bullseye(sym, center, 7);
            /* reference grid every 16 modules out to the edge */
            for (j = 0; j <= center; j += 16)
                for (k = center & 1; k < size; k += 2) {
                    TDBG_SET(sym, center - j, k, 1); TDBG_SET(sym, center + j, k, 1);
                    TDBG_SET(sym, k, center - j, 1); TDBG_SET(sym, k, center + j, 1);
                }
        }
    }
    *out = sym;
    sym = NULL;

done:
    tdbg_symbol_free(sym);
    tdbg_bits_free(&bits);
    tdbg_bits_free(&msg);
    free(words);
    free(amap);
    return rc;
}
