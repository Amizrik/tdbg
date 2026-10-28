/* MaxiCode encoder (ISO/IEC 16023:2000), modes 2-6. */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/*
 * Module layout of the 33x30 symbol, from ISO/IEC 16023 Figure 5:
 * value >= 0  -> bit number (codeword = v / 6, bit 0 of a codeword is its MSB)
 * -1 light orientation module, -2 dark orientation module,
 * -3 unused (central finder area / missing half-module on odd rows).
 * Transcription as published in ZXing (Apache License 2.0).
 */
static const short MAXI_GRID[33][30] = {
    {121,120,127,126,133,132,139,138,145,144,151,150,157,156,163,162,169,168,175,174,181,180,187,186,193,192,199,198, -2, -2},
    {123,122,129,128,135,134,141,140,147,146,153,152,159,158,165,164,171,170,177,176,183,182,189,188,195,194,201,200,816, -3},
    {125,124,131,130,137,136,143,142,149,148,155,154,161,160,167,166,173,172,179,178,185,184,191,190,197,196,203,202,818,817},
    {283,282,277,276,271,270,265,264,259,258,253,252,247,246,241,240,235,234,229,228,223,222,217,216,211,210,205,204,819, -3},
    {285,284,279,278,273,272,267,266,261,260,255,254,249,248,243,242,237,236,231,230,225,224,219,218,213,212,207,206,821,820},
    {287,286,281,280,275,274,269,268,263,262,257,256,251,250,245,244,239,238,233,232,227,226,221,220,215,214,209,208,822, -3},
    {289,288,295,294,301,300,307,306,313,312,319,318,325,324,331,330,337,336,343,342,349,348,355,354,361,360,367,366,824,823},
    {291,290,297,296,303,302,309,308,315,314,321,320,327,326,333,332,339,338,345,344,351,350,357,356,363,362,369,368,825, -3},
    {293,292,299,298,305,304,311,310,317,316,323,322,329,328,335,334,341,340,347,346,353,352,359,358,365,364,371,370,827,826},
    {409,408,403,402,397,396,391,390, 79, 78, -2, -2, 13, 12, 37, 36,  2, -1, 44, 43,109,108,385,384,379,378,373,372,828, -3},
    {411,410,405,404,399,398,393,392, 81, 80, 40, -2, 15, 14, 39, 38,  3, -1, -1, 45,111,110,387,386,381,380,375,374,830,829},
    {413,412,407,406,401,400,395,394, 83, 82, 41, -3, -3, -3, -3, -3,  5,  4, 47, 46,113,112,389,388,383,382,377,376,831, -3},
    {415,414,421,420,427,426,103,102, 55, 54, 16, -3, -3, -3, -3, -3, -3, -3, 20, 19, 85, 84,433,432,439,438,445,444,833,832},
    {417,416,423,422,429,428,105,104, 57, 56, -3, -3, -3, -3, -3, -3, -3, -3, 22, 21, 87, 86,435,434,441,440,447,446,834, -3},
    {419,418,425,424,431,430,107,106, 59, 58, -3, -3, -3, -3, -3, -3, -3, -3, -3, 23, 89, 88,437,436,443,442,449,448,836,835},
    {481,480,475,474,469,468, 48, -2, 30, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3,  0, 53, 52,463,462,457,456,451,450,837, -3},
    {483,482,477,476,471,470, 49, -1, -2, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -2, -1,465,464,459,458,453,452,839,838},
    {485,484,479,478,473,472, 51, 50, 31, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3,  1, -2, 42,467,466,461,460,455,454,840, -3},
    {487,486,493,492,499,498, 97, 96, 61, 60, -3, -3, -3, -3, -3, -3, -3, -3, -3, 26, 91, 90,505,504,511,510,517,516,842,841},
    {489,488,495,494,501,500, 99, 98, 63, 62, -3, -3, -3, -3, -3, -3, -3, -3, 28, 27, 93, 92,507,506,513,512,519,518,843, -3},
    {491,490,497,496,503,502,101,100, 65, 64, 17, -3, -3, -3, -3, -3, -3, -3, 18, 29, 95, 94,509,508,515,514,521,520,845,844},
    {559,558,553,552,547,546,541,540, 73, 72, 32, -3, -3, -3, -3, -3, -3, 10, 67, 66,115,114,535,534,529,528,523,522,846, -3},
    {561,560,555,554,549,548,543,542, 75, 74, -2, -1,  7,  6, 35, 34, 11, -2, 69, 68,117,116,537,536,531,530,525,524,848,847},
    {563,562,557,556,551,550,545,544, 77, 76, -2, 33,  9,  8, 25, 24, -1, -2, 71, 70,119,118,539,538,533,532,527,526,849, -3},
    {565,564,571,570,577,576,583,582,589,588,595,594,601,600,607,606,613,612,619,618,625,624,631,630,637,636,643,642,851,850},
    {567,566,573,572,579,578,585,584,591,590,597,596,603,602,609,608,615,614,621,620,627,626,633,632,639,638,645,644,852, -3},
    {569,568,575,574,581,580,587,586,593,592,599,598,605,604,611,610,617,616,623,622,629,628,635,634,641,640,647,646,854,853},
    {727,726,721,720,715,714,709,708,703,702,697,696,691,690,685,684,679,678,673,672,667,666,661,660,655,654,649,648,855, -3},
    {729,728,723,722,717,716,711,710,705,704,699,698,693,692,687,686,681,680,675,674,669,668,663,662,657,656,651,650,857,856},
    {731,730,725,724,719,718,713,712,707,706,701,700,695,694,689,688,683,682,677,676,671,670,665,664,659,658,653,652,858, -3},
    {733,732,739,738,745,744,751,750,757,756,763,762,769,768,775,774,781,780,787,786,793,792,799,798,805,804,811,810,860,859},
    {735,734,741,740,747,746,753,752,759,758,765,764,771,770,777,776,783,782,789,788,795,794,801,800,807,806,813,812,861, -3},
    {737,736,743,742,749,748,755,754,761,760,767,766,773,772,779,778,785,784,791,790,797,796,803,802,809,808,815,814,863,862}
};

/* Code sets (Table 2). Special codes shared by the sets: */
#define CS_NS     31   /* numeric shift: 9 digits in 5 codewords */
#define CS_PAD    33
#define CS_SHIFT  59   /* A: Shift B, B: Shift A */
#define CS_SHIFTC 60
#define CS_SHIFTD 61
#define CS_SHIFTE 62
#define CS_LATCH  63   /* A: Latch B, B: Latch A */

enum { SET_A, SET_B, SET_C, SET_D, SET_E, NSETS };

static signed char set_val[NSETS][256];
static int tables_ready;

static void put_list(int set, int start, const unsigned char *chars, int n)
{
    int i;
    for (i = 0; i < n; i++) set_val[set][chars[i]] = (signed char)(start + i);
}

static void put_range(int set, int start, int lo, int hi)
{
    int c;
    for (c = lo; c <= hi; c++) set_val[set][c] = (signed char)(start + c - lo);
}

static void init_tables(void)
{
    static const unsigned char a_punct[] = "\"#$%&'()*+,-./";
    static const unsigned char b_punct[] = "}~\x7f;<=>?[\\]^_ ,./:@!|";
    static const unsigned char c_hi[] = { 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xAA, 0xAC, 0xB1,
                                          0xB2, 0xB3, 0xB5, 0xB9, 0xBA, 0xBC, 0xBD, 0xBE };
    static const unsigned char d_hi[] = { 0xFB, 0xFC, 0xFD, 0xFE, 0xFF, 0xA1, 0xA8, 0xAB,
                                          0xAF, 0xB0, 0xB4, 0xB7, 0xB8, 0xBB, 0xBF };
    static const unsigned char e_hi[] = { 0x1C, 0x1D, 0x1E, 0x1F, 0x9F, 0xA0, 0xA2, 0xA3, 0xA4,
                                          0xA5, 0xA6, 0xA7, 0xA9, 0xAD, 0xAE, 0xB6 };
    int s;
    if (tables_ready) return;
    memset(set_val, -1, sizeof set_val);

    set_val[SET_A]['\r'] = 0;
    put_range(SET_A, 1, 'A', 'Z');
    set_val[SET_A][' '] = 32;
    put_list(SET_A, 34, a_punct, 14);
    put_range(SET_A, 48, '0', '9');
    set_val[SET_A][':'] = 58;

    set_val[SET_B]['`'] = 0;
    put_range(SET_B, 1, 'a', 'z');
    set_val[SET_B]['{'] = 32;
    put_list(SET_B, 34, b_punct, 21);

    put_range(SET_C, 0, 0xC0, 0xDA);
    put_list(SET_C, 32, c_hi, 16);
    put_range(SET_C, 48, 0x80, 0x89);
    set_val[SET_C][' '] = 59;

    put_range(SET_D, 0, 0xE0, 0xFA);
    put_list(SET_D, 32, d_hi, 15);
    put_range(SET_D, 47, 0x8A, 0x94);
    set_val[SET_D][' '] = 59;

    put_range(SET_E, 0, 0x00, 0x1A);
    set_val[SET_E][0x1B] = 30;
    put_list(SET_E, 32, e_hi, 16);
    put_range(SET_E, 48, 0x95, 0x9E);
    set_val[SET_E][' '] = 59;

    /* FS, GS, RS are 28-30 in sets A-D */
    for (s = SET_A; s <= SET_D; s++) {
        set_val[s][0x1C] = 28;
        set_val[s][0x1D] = 29;
        set_val[s][0x1E] = 30;
    }
    tables_ready = 1;
}

static int digit_run(const unsigned char *d, int len, int i)
{
    int n = 0;
    while (i + n < len && d[i + n] >= '0' && d[i + n] <= '9') n++;
    return n;
}

/* Text -> 6-bit codewords. Returns count, or -1 if more than `cap` needed. */
static int encode_message(const unsigned char *d, int len, unsigned char *cw, int cap)
{
    int set = SET_A, i = 0, n = 0;
#define EMIT(v) do { if (n >= cap) return -1; cw[n++] = (unsigned char)(v); } while (0)
    while (i < len) {
        unsigned char c = d[i];
        int other = set == SET_A ? SET_B : SET_A;

        if (digit_run(d, len, i) >= 9) {
            long v = 0;
            int k;
            for (k = 0; k < 9; k++) v = v * 10 + (d[i + k] - '0');
            EMIT(CS_NS);
            for (k = 4; k >= 0; k--) EMIT((v >> (6 * k)) & 0x3F);
            i += 9;
            continue;
        }
        if (set_val[set][c] >= 0) {
            EMIT(set_val[set][c]);
        } else if (set_val[other][c] >= 0) {
            int latch = i + 1 < len && set_val[other][d[i + 1]] >= 0 && set_val[set][d[i + 1]] < 0;
            EMIT(latch ? CS_LATCH : CS_SHIFT);
            EMIT(set_val[other][c]);
            if (latch) set = other;
        } else {
            int s = set_val[SET_C][c] >= 0 ? SET_C : set_val[SET_D][c] >= 0 ? SET_D : SET_E;
            EMIT(s == SET_C ? CS_SHIFTC : s == SET_D ? CS_SHIFTD : CS_SHIFTE);
            EMIT(set_val[s][c]);
        }
        i++;
    }
#undef EMIT
    return n;
}

static int rs6(const unsigned char *in, int n, int step, unsigned char *out, int ostep, int necc)
{
    int data[84], ecc[56], i, rc;
    for (i = 0; i < n; i++) data[i] = in[i * step];
    rc = tdbg_rs_encode(6, 0x43, 1, data, n, ecc, necc);
    for (i = 0; i < necc; i++) out[i * ostep] = (unsigned char)ecc[i];
    return rc;
}

int tdbg_maxicode_encode(int mode, const unsigned char *data, int len,
                       const char *postcode, int country, int service, tdbg_symbol **out)
{
    unsigned char cw[144], msg[144];
    int nmsg, sec_len, sec_ecc, primary_data, i, x, y, rc;
    tdbg_symbol *sym;

    if (!out || (!data && len) || len < 0 || mode < 2 || mode > 6) return TDBG_ERR_ARG;
    *out = NULL;
    init_tables();

    sec_len = mode == 5 ? 68 : 84;
    sec_ecc = mode == 5 ? 56 : 40;
    primary_data = (mode == 2 || mode == 3) ? 0 : 9; /* message codewords in primary */

    nmsg = encode_message(data, len, msg, primary_data + sec_len);
    if (nmsg < 0) return TDBG_ERR_TOO_LONG;
    for (i = nmsg; i < primary_data + sec_len; i++) msg[i] = CS_PAD;

    memset(cw, 0, sizeof cw);
    if (mode == 2 || mode == 3) {
        int plen = postcode ? (int)strlen(postcode) : 0;
        if (country < 0 || country > 999 || service < 0 || service > 999) return TDBG_ERR_ARG;
        if (mode == 2) {
            long pc = 0;
            if (plen < 1 || plen > 9) return TDBG_ERR_ARG;
            for (i = 0; i < plen; i++) {
                if (postcode[i] < '0' || postcode[i] > '9') return TDBG_ERR_UNENCODABLE;
                pc = pc * 10 + (postcode[i] - '0');
            }
            cw[0] = (unsigned char)(((pc & 0x03) << 4) | 2);
            cw[1] = (unsigned char)((pc & 0xFC) >> 2);
            cw[2] = (unsigned char)((pc & 0x3F00) >> 8);
            cw[3] = (unsigned char)((pc & 0xFC000) >> 14);
            cw[4] = (unsigned char)((pc & 0x3F00000) >> 20);
            cw[5] = (unsigned char)(((pc & 0x3C000000) >> 26) | ((plen & 0x3) << 4));
            cw[6] = (unsigned char)(((plen & 0x3C) >> 2) | ((country & 0x3) << 4));
        } else {
            int p[6];
            if (plen > 6) return TDBG_ERR_ARG;
            for (i = 0; i < 6; i++) {
                unsigned char c = i < plen ? (unsigned char)postcode[i] : ' ';
                if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
                if (set_val[SET_A][c] < 0) return TDBG_ERR_UNENCODABLE;
                p[i] = set_val[SET_A][c];
            }
            cw[0] = (unsigned char)(((p[5] & 0x03) << 4) | 3);
            cw[1] = (unsigned char)(((p[4] & 0x03) << 4) | ((p[5] & 0x3C) >> 2));
            cw[2] = (unsigned char)(((p[3] & 0x03) << 4) | ((p[4] & 0x3C) >> 2));
            cw[3] = (unsigned char)(((p[2] & 0x03) << 4) | ((p[3] & 0x3C) >> 2));
            cw[4] = (unsigned char)(((p[1] & 0x03) << 4) | ((p[2] & 0x3C) >> 2));
            cw[5] = (unsigned char)(((p[0] & 0x03) << 4) | ((p[1] & 0x3C) >> 2));
            cw[6] = (unsigned char)(((p[0] & 0x3C) >> 2) | ((country & 0x3) << 4));
        }
        cw[7] = (unsigned char)((country & 0xFC) >> 2);
        cw[8] = (unsigned char)(((country & 0x300) >> 8) | ((service & 0xF) << 2));
        cw[9] = (unsigned char)((service & 0x3F0) >> 4);
    } else {
        cw[0] = (unsigned char)mode;
        memcpy(cw + 1, msg, 9);
    }
    memcpy(cw + 20, msg + primary_data, (size_t)sec_len);

    /* primary: 10 data + 10 check words; secondary: two interleaved RS codes */
    if ((rc = rs6(cw, 10, 1, cw + 10, 1, 10)) != TDBG_OK) return rc;
    if ((rc = rs6(cw + 20, sec_len / 2, 2, cw + 20 + sec_len, 2, sec_ecc / 2)) != TDBG_OK) return rc;
    if ((rc = rs6(cw + 21, sec_len / 2, 2, cw + 21 + sec_len, 2, sec_ecc / 2)) != TDBG_OK) return rc;

    sym = tdbg_symbol_new(30, 33, TDBG_GRID_HEX, 1);
    if (!sym) return TDBG_ERR_NOMEM;
    for (y = 0; y < 33; y++)
        for (x = 0; x < 30; x++) {
            int v = MAXI_GRID[y][x];
            if (v >= 0) TDBG_SET(sym, x, y, (cw[v / 6] >> (5 - v % 6)) & 1);
            else TDBG_SET(sym, x, y, v == -2);
        }
    *out = sym;
    return TDBG_OK;
}
