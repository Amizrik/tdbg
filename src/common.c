/* Shared infrastructure: bit buffer, Reed-Solomon, symbol objects, SVG output. */
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ---------------------------------------------------------------- bits */

void tdbg_bits_init(tdbg_bits *b) { memset(b, 0, sizeof *b); }

void tdbg_bits_free(tdbg_bits *b) { free(b->bits); tdbg_bits_init(b); }

void tdbg_bits_put(tdbg_bits *b, unsigned value, int nbits)
{
    int i;
    if (b->len + nbits > b->cap) {
        int ncap = b->cap ? b->cap * 2 : 256;
        unsigned char *p;
        while (ncap < b->len + nbits) ncap *= 2;
        p = (unsigned char *)realloc(b->bits, (size_t)ncap);
        if (!p) { b->oom = 1; return; }
        b->bits = p;
        b->cap = ncap;
    }
    for (i = nbits - 1; i >= 0; i--)
        b->bits[b->len++] = (unsigned char)((value >> i) & 1u);
}

unsigned tdbg_bits_get(const tdbg_bits *b, int pos, int nbits)
{
    unsigned v = 0;
    int i;
    for (i = 0; i < nbits; i++)
        v = (v << 1) | (pos + i < b->len ? b->bits[pos + i] : 0u);
    return v;
}

/* ---------------------------------------------------------- Reed-Solomon */

int tdbg_rs_encode(int m, int poly, int base, const int *data, int ndata,
                 int *ecc, int necc)
{
    int size = 1 << m, i, j, x;
    int *exp_t, *log_t, *gen;

    if (m < 2 || m > 12 || necc <= 0) return TDBG_ERR_ARG;
    exp_t = (int *)malloc(sizeof(int) * (size_t)size * 2);
    log_t = (int *)malloc(sizeof(int) * (size_t)size);
    gen = (int *)malloc(sizeof(int) * (size_t)(necc + 1));
    if (!exp_t || !log_t || !gen) {
        free(exp_t); free(log_t); free(gen);
        return TDBG_ERR_NOMEM;
    }

    /* log/antilog tables; exp_t is doubled so a+b needs no modulo */
    x = 1;
    for (i = 0; i < size - 1; i++) {
        exp_t[i] = exp_t[i + size - 1] = x;
        log_t[x] = i;
        x <<= 1;
        if (x & size) x ^= poly;
    }
    log_t[0] = 0;

#define GF_MUL(a, b) (((a) && (b)) ? exp_t[log_t[a] + log_t[b]] : 0)

    /* generator polynomial, gen[0] = coefficient of x^necc */
    gen[0] = 1;
    for (i = 1; i <= necc; i++) gen[i] = 0;
    for (i = 0; i < necc; i++) {
        int root = exp_t[(base + i) % (size - 1)];
        for (j = i + 1; j >= 1; j--)
            gen[j] ^= GF_MUL(gen[j - 1], root);
    }

    /* polynomial division: remainder of data(x) * x^necc / gen(x) */
    for (i = 0; i < necc; i++) ecc[i] = 0;
    for (i = 0; i < ndata; i++) {
        int f = data[i] ^ ecc[0];
        memmove(ecc, ecc + 1, sizeof(int) * (size_t)(necc - 1));
        ecc[necc - 1] = 0;
        if (f)
            for (j = 0; j < necc; j++) ecc[j] ^= GF_MUL(gen[j + 1], f);
    }
#undef GF_MUL

    free(exp_t); free(log_t); free(gen);
    return TDBG_OK;
}

/* -------------------------------------------------------------- symbols */

tdbg_symbol *tdbg_symbol_new(int width, int height, int grid, int quiet)
{
    tdbg_symbol *s = (tdbg_symbol *)malloc(sizeof *s);
    if (!s) return NULL;
    s->modules = (unsigned char *)calloc((size_t)width * (size_t)height, 1);
    if (!s->modules) { free(s); return NULL; }
    s->width = width;
    s->height = height;
    s->grid = grid;
    s->quiet_zone = quiet;
    return s;
}

void tdbg_symbol_free(tdbg_symbol *sym)
{
    if (!sym) return;
    free(sym->modules);
    free(sym);
}

void tdbg_free(void *p) { free(p); }

const char *tdbg_version(void) { return TDBG_VERSION; }

const char *tdbg_strerror(int code)
{
    switch (code) {
    case TDBG_OK: return "ok";
    case TDBG_ERR_ARG: return "invalid argument";
    case TDBG_ERR_TOO_LONG: return "data too long for symbol";
    case TDBG_ERR_NOMEM: return "out of memory";
    case TDBG_ERR_UNENCODABLE: return "data cannot be encoded in this mode";
    default: return "unknown error";
    }
}

/* ------------------------------------------------------------------ SVG */

typedef struct { char *p; size_t len, cap; int oom; } sbuf;

static void sb_printf(sbuf *s, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (s->oom) return;
    for (;;) {
        size_t room = s->cap - s->len;
        va_start(ap, fmt);
        n = vsnprintf(s->p + s->len, room, fmt, ap);
        va_end(ap);
        if (n < 0) { s->oom = 1; return; }
        if ((size_t)n < room) { s->len += (size_t)n; return; }
        {
            size_t ncap = s->cap * 2 + (size_t)n + 1;
            char *np = (char *)realloc(s->p, ncap);
            if (!np) { s->oom = 1; return; }
            s->p = np;
            s->cap = ncap;
        }
    }
}

/* Geometry of the MaxiCode hex grid, in units of the horizontal module pitch. */
#define HEX_ROW_PITCH 0.86602540378443865   /* sqrt(3)/2 */
#define HEX_R         0.57735026918962576   /* centre-to-vertex, 1/sqrt(3) */

char *tdbg_svg(const tdbg_symbol *sym, double ms, int qz)
{
    sbuf s;
    int x, y;
    double w, h;

    if (!sym || ms <= 0) return NULL;
    if (qz < 0) qz = sym->quiet_zone;
    s.cap = 4096; s.len = 0; s.oom = 0;
    s.p = (char *)malloc(s.cap);
    if (!s.p) return NULL;
    s.p[0] = 0;

    if (sym->grid == TDBG_GRID_HEX) {
        w = (sym->width + 2 * qz) * ms;   /* odd rows have one hexagon fewer */
        h = ((sym->height - 1) * HEX_ROW_PITCH + 2 * HEX_R + 2 * qz) * ms;
    } else {
        w = (sym->width + 2 * qz) * ms;
        h = (sym->height + 2 * qz) * ms;
    }
    sb_printf(&s, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                  "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" "
                  "width=\"%g\" height=\"%g\" viewBox=\"0 0 %g %g\" shape-rendering=\"%s\">\n"
                  "<rect width=\"100%%\" height=\"100%%\" fill=\"#fff\"/>\n<path fill=\"#000\" d=\"",
              w, h, w, h, sym->grid == TDBG_GRID_HEX ? "geometricPrecision" : "crispEdges");

    if (sym->grid == TDBG_GRID_HEX) {
        double ox = qz * ms, oy = qz * ms;
        for (y = 0; y < sym->height; y++)
            for (x = 0; x < sym->width; x++) {
                double cx, cy;
                if (!TDBG_GET(sym, x, y)) continue;
                cx = ox + (x + 0.5 + (y & 1) * 0.5) * ms;
                cy = oy + (HEX_R + y * HEX_ROW_PITCH) * ms;
                /* pointy-top hexagon, slightly shrunk so neighbours stay distinct */
                {
                    double r = HEX_R * ms * 0.94, hw = 0.5 * ms * 0.94;
                    sb_printf(&s, "M%.3f %.3fL%.3f %.3fL%.3f %.3fL%.3f %.3fL%.3f %.3fL%.3f %.3fZ",
                              cx, cy - r, cx + hw, cy - r / 2, cx + hw, cy + r / 2,
                              cx, cy + r, cx - hw, cy + r / 2, cx - hw, cy - r / 2);
                }
            }
        sb_printf(&s, "\"/>\n");
        /* bullseye: three dark rings around a light centre */
        {
            double cx = ox + 14.5 * ms;
            double cy = oy + (HEX_R + 16 * HEX_ROW_PITCH) * ms;
            /* ISO/IEC 16023 4.2.1.1: inner light disc diameter V = 2/sqrt(3),
               overall finder diameter 9X, five equal diameter steps between */
            static const double radii[6] = { 4.5, 3.7155, 2.9309, 2.1464, 1.3619, HEX_R };
            int i;
            for (i = 0; i < 6; i++)
                sb_printf(&s, "<circle cx=\"%.3f\" cy=\"%.3f\" r=\"%.3f\" fill=\"%s\"/>\n",
                          cx, cy, radii[i] * ms, (i & 1) ? "#fff" : "#000");
        }
    } else {
        /* merge horizontal runs into rectangles to keep the file small */
        for (y = 0; y < sym->height; y++) {
            x = 0;
            while (x < sym->width) {
                int run;
                if (!TDBG_GET(sym, x, y)) { x++; continue; }
                for (run = 1; x + run < sym->width && TDBG_GET(sym, x + run, y); run++) {}
                sb_printf(&s, "M%g %gh%gv%gh-%gz", (x + qz) * ms, (y + qz) * ms, run * ms, ms, run * ms);
                x += run;
            }
        }
        sb_printf(&s, "\"/>\n");
    }
    sb_printf(&s, "</svg>\n");
    if (s.oom) { free(s.p); return NULL; }
    return s.p;
}
