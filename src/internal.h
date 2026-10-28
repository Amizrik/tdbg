/* Internal helpers shared by the encoders. Not part of the public API. */
#ifndef TDBG_INTERNAL_H
#define TDBG_INTERNAL_H

#include "tdbg.h"
#include <stddef.h>

/* Growable bit buffer, one byte per bit (simple and fast enough). */
typedef struct {
    unsigned char *bits;
    int len, cap;
    int oom;           /* set when an allocation failed */
} tdbg_bits;

void tdbg_bits_init(tdbg_bits *b);
void tdbg_bits_free(tdbg_bits *b);
void tdbg_bits_put(tdbg_bits *b, unsigned value, int nbits);   /* MSB first */
unsigned tdbg_bits_get(const tdbg_bits *b, int pos, int nbits); /* MSB first, past end = 0 */

/*
 * Reed-Solomon over GF(2^m), m <= 12, field polynomial `poly`.
 * Generator = prod_{i=0}^{necc-1} (x - a^(base+i)).
 * ecc[0] is the highest-degree check word (i.e. sent right after the data).
 */
int tdbg_rs_encode(int m, int poly, int base, const int *data, int ndata,
                 int *ecc, int necc);

tdbg_symbol *tdbg_symbol_new(int width, int height, int grid, int quiet);

#define TDBG_SET(sym, x, y, v) ((sym)->modules[(y) * (sym)->width + (x)] = (unsigned char)(v))
#define TDBG_GET(sym, x, y)    ((sym)->modules[(y) * (sym)->width + (x)])

#endif
