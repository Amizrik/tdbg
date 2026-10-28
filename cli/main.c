/* tdbg command-line tool. */
#include "tdbg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void)
{
    fprintf(stderr,
        "usage: tdbg <qr|datamatrix|aztec|maxicode> [options] <text>\n"
        "  -o FILE      output .svg or .pbm (default: text preview on stdout)\n"
        "  -s N         module size for SVG/PBM (default 4)\n"
        "  -e LEVEL     qr: L/M/Q/H   aztec: ECC percent (default 23)\n"
        "  -v N         qr: minimum version; aztec: layers (+compact/-full)\n"
        "  -r           datamatrix: rectangular symbol\n"
        "  -m MODE      maxicode: mode 2-6 (default 4)\n"
        "  -p POSTCODE  maxicode modes 2/3\n"
        "  -c COUNTRY   maxicode modes 2/3 (numeric)\n"
        "  -k SERVICE   maxicode modes 2/3 (numeric)\n");
}

static int write_pbm(const tdbg_symbol *s, const char *path, int scale)
{
    FILE *f = fopen(path, "wb");
    int qz = s->quiet_zone, w = (s->width + 2 * qz) * scale, h = (s->height + 2 * qz) * scale, x, y;
    if (!f) return -1;
    fprintf(f, "P1\n%d %d\n", w, h);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int mx = x / scale - qz, my = y / scale - qz;
            int dark = mx >= 0 && my >= 0 && mx < s->width && my < s->height &&
                       s->modules[my * s->width + mx];
            fputc(dark ? '1' : '0', f);
            fputc(x + 1 < w ? ' ' : '\n', f);
        }
    }
    return fclose(f);
}

static void print_text(const tdbg_symbol *s)
{
    int qz = 2, x, y;
    for (y = -qz; y < s->height + qz; y++) {
        for (x = -qz; x < s->width + qz; x++) {
            int dark = x >= 0 && y >= 0 && x < s->width && y < s->height && s->modules[y * s->width + x];
            if (s->grid == TDBG_GRID_HEX && x == -qz && (y & 1)) putchar(' ');
            fputs(dark ? "##" : "  ", stdout);
        }
        putchar('\n');
    }
}

int main(int argc, char **argv)
{
    const char *type, *text = NULL, *outpath = NULL, *ecc = NULL, *postcode = NULL;
    int i, scale = 4, ver = 0, rect = 0, mode = 4, country = 0, service = 0, rc;
    tdbg_symbol *sym = NULL;

    if (argc < 3) { usage(); return 2; }
    type = argv[1];
    for (i = 2; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '-' && a[1] && !a[2] && strchr("osevmpck", a[1]) && i + 1 < argc) {
            const char *v = argv[++i];
            switch (a[1]) {
            case 'o': outpath = v; break;
            case 's': scale = atoi(v); break;
            case 'e': ecc = v; break;
            case 'v': ver = atoi(v); break;
            case 'm': mode = atoi(v); break;
            case 'p': postcode = v; break;
            case 'c': country = atoi(v); break;
            case 'k': service = atoi(v); break;
            }
        } else if (!strcmp(a, "-r")) {
            rect = 1;
        } else {
            text = a;
        }
    }
    if (!text) { usage(); return 2; }

    if (!strcmp(type, "qr")) {
        int level = TDBG_QR_ECC_M;
        if (ecc) level = (int)(strchr("LMQH", ecc[0] & ~0x20) ? strchr("LMQH", ecc[0] & ~0x20) - "LMQH" : 1);
        rc = tdbg_qr_encode((const unsigned char *)text, (int)strlen(text), level, ver, -1, &sym);
    } else if (!strcmp(type, "datamatrix") || !strcmp(type, "dm")) {
        rc = tdbg_datamatrix_encode((const unsigned char *)text, (int)strlen(text),
                                  rect ? TDBG_DM_RECT : TDBG_DM_SQUARE, &sym);
    } else if (!strcmp(type, "aztec")) {
        rc = tdbg_aztec_encode((const unsigned char *)text, (int)strlen(text), ecc ? atoi(ecc) : 23, ver, &sym);
    } else if (!strcmp(type, "maxicode") || !strcmp(type, "maxi")) {
        rc = tdbg_maxicode_encode(mode, (const unsigned char *)text, (int)strlen(text),
                                postcode, country, service, &sym);
    } else {
        usage();
        return 2;
    }
    if (rc != TDBG_OK) {
        fprintf(stderr, "tdbg: %s\n", tdbg_strerror(rc));
        return 1;
    }

    if (!outpath) {
        print_text(sym);
    } else {
        size_t n = strlen(outpath);
        if (n > 4 && !strcmp(outpath + n - 4, ".pbm")) {
            if (sym->grid == TDBG_GRID_HEX) {
                fprintf(stderr, "tdbg: MaxiCode needs SVG output\n");
                rc = 1;
            } else if (write_pbm(sym, outpath, scale)) {
                perror(outpath);
                rc = 1;
            }
        } else {
            char *svg = tdbg_svg(sym, scale, -1);
            FILE *f = svg ? fopen(outpath, "wb") : NULL;
            if (!f || fputs(svg, f) < 0 || fclose(f)) { perror(outpath); rc = 1; }
            tdbg_free(svg);
        }
    }
    tdbg_symbol_free(sym);
    return rc;
}
