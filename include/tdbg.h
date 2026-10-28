/*
 * tdbg - portable 2D barcode generator (QR Code, Data Matrix, Aztec, MaxiCode)
 *
 * Pure C99, no dependencies. All encoders produce a tdbg_symbol: a module
 * matrix (1 = dark) that callers render however they like. tdbg_svg() is a
 * ready-made renderer, including hexagonal MaxiCode output.
 *
 * The API uses only ints, byte buffers and one plain struct so it can be
 * called through any FFI (Python ctypes/cffi, C#, Go, Rust, ...).
 */
#ifndef TDBG_H
#define TDBG_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#  ifdef TDBG_BUILD
#    define TDBG_API __declspec(dllexport)
#  elif defined(TDBG_STATIC)
#    define TDBG_API
#  else
#    define TDBG_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) && __GNUC__ >= 4
#  define TDBG_API __attribute__((visibility("default")))
#else
#  define TDBG_API
#endif

#define TDBG_VERSION "1.0.0"

/* Error codes (all negative). */
#define TDBG_OK                 0
#define TDBG_ERR_ARG           -1  /* invalid argument */
#define TDBG_ERR_TOO_LONG      -2  /* data does not fit the largest allowed symbol */
#define TDBG_ERR_NOMEM         -3
#define TDBG_ERR_UNENCODABLE   -4  /* data contains characters the mode cannot carry */

/* Module geometry of a tdbg_symbol. */
#define TDBG_GRID_SQUARE  0   /* ordinary square modules */
#define TDBG_GRID_HEX     1   /* MaxiCode: hexagonal modules, odd rows shifted right
                               by half a module, row pitch = sqrt(3)/2, with a
                               bullseye finder drawn at the centre */

typedef struct tdbg_symbol {
    int width;               /* modules per row */
    int height;              /* number of rows */
    int grid;                /* TDBG_GRID_SQUARE or TDBG_GRID_HEX */
    int quiet_zone;          /* recommended quiet zone in modules */
    unsigned char *modules;  /* width*height bytes, row-major, 1 = dark */
} tdbg_symbol;

/* QR Code error correction levels. */
#define TDBG_QR_ECC_L 0
#define TDBG_QR_ECC_M 1
#define TDBG_QR_ECC_Q 2
#define TDBG_QR_ECC_H 3

/*
 * QR Code (ISO/IEC 18004), versions 1-40, numeric/alphanumeric/byte segments.
 *   ecc_level   TDBG_QR_ECC_L..H
 *   version     0 = smallest that fits, 1..40 = minimum version to use
 *   mask        -1 = automatic (lowest penalty), 0..7 = forced
 */
TDBG_API int tdbg_qr_encode(const unsigned char *data, int len, int ecc_level,
                        int version, int mask, tdbg_symbol **out);

/* Data Matrix ECC 200 (ISO/IEC 16022). */
#define TDBG_DM_SQUARE  0
#define TDBG_DM_RECT    1
#define TDBG_DM_ANY     2   /* smallest symbol of either shape */
TDBG_API int tdbg_datamatrix_encode(const unsigned char *data, int len, int shape,
                                tdbg_symbol **out);

/*
 * Aztec Code (ISO/IEC 24778).
 *   ecc_percent  error correction share, 5..95 (spec recommends 23 + 3 words)
 *   layers       0 = automatic, 1..4 = compact with that many layers,
 *                -1..-32 = full-range with |layers| layers
 */
TDBG_API int tdbg_aztec_encode(const unsigned char *data, int len, int ecc_percent,
                           int layers, tdbg_symbol **out);

/*
 * MaxiCode (ISO/IEC 16023), always 33 rows x 30 columns.
 *   mode       2 (numeric postcode), 3 (alphanumeric postcode),
 *              4 (standard), 5 (full ECC), 6 (reader programming)
 *   postcode   modes 2/3 only: up to 9 digits (mode 2) / 6 chars (mode 3)
 *   country    modes 2/3: ISO 3166 numeric country code (0..999)
 *   service    modes 2/3: service class (0..999)
 */
TDBG_API int tdbg_maxicode_encode(int mode, const unsigned char *data, int len,
                              const char *postcode, int country, int service,
                              tdbg_symbol **out);

TDBG_API void tdbg_symbol_free(tdbg_symbol *sym);

/* Render to an SVG document (malloc'd, NUL-terminated; free with tdbg_free).
 * module_size is in SVG user units; quiet_zone < 0 uses sym->quiet_zone. */
TDBG_API char *tdbg_svg(const tdbg_symbol *sym, double module_size, int quiet_zone);
TDBG_API void tdbg_free(void *p);

TDBG_API const char *tdbg_strerror(int code);
TDBG_API const char *tdbg_version(void);

#ifdef __cplusplus
}
#endif
#endif /* TDBG_H */
