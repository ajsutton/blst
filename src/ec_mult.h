/*
 * Copyright Supranational LLC
 * Licensed under the Apache License, Version 2.0, see LICENSE for details.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BLS12_381_ASM_EC_MULT_H__
#define __BLS12_381_ASM_EC_MULT_H__

#include "point.h"

/* Works up to 9 bits */
static limb_t get_wval(const byte *d, size_t off, size_t bits)
{
    size_t top = off + bits - 1;
    limb_t ret;

    ret = ((limb_t)d[top / 8] << 8) | d[off / 8];
    ret >>= (off % 8);

    return ret;
}

/*
 * Window value encoding that utilizes the fact that -P is trivially
 * calculated, which allows to halve the size of pre-computed table,
 * is attributed to A. D. Booth, hence the name of the subroutines...
 */
static limb_t booth_encode(limb_t wval, int sz)
{
    limb_t mask = 0 - (wval >> sz);     /* "sign" bit -> mask */

    wval = (wval + 1) >> 1;
    wval = (wval & ~mask) | ((0-wval) & mask);

    /* &0x1f, but <=0x10, is index in table, rest is extended "sign" bit */
    return wval;
}

/*
 * Key feature of these constant-time subroutines is that they tolerate
 * zeros in most significant bit positions of the scalar[s], or in other
 * words, zero-padded scalar values. This means that one can and should
 * pass order's bit-length, which is customarily publicly known, instead
 * of the factual scalars' bit-lengths. This is facilitated by point
 * addition subroutines implemented to handle points at infinity, which
 * are encoded as Z==0. [Doubling agorithms handle such points at
 * infinity "naturally," since resulting Z is product of original Z.]
 */
#define POINT_MULT_SCALAR_WX_IMPL(ptype, SZ) \
static void ptype##_gather_booth_w##SZ(ptype *restrict p, \
                                       const ptype table[1<<(SZ-1)], \
                                       limb_t booth_idx) \
{ \
    size_t i; \
    limb_t booth_sign = (booth_idx >> SZ) & 1; \
\
    booth_idx &= (1<<SZ) - 1; \
    vec_zero(p, sizeof(ptype)); /* implicit infinity at table[-1] */\
    /* ~6% with -Os, ~2% with -O3 ... */\
    for (i = 1; i <= 1<<(SZ-1); i++) \
        ptype##_ccopy(p, table + i - 1, i == booth_idx); \
\
    ptype##_cneg(p, booth_sign); \
} \
\
static void ptype##_precompute_w##SZ(ptype *row, const ptype *point) \
{ \
    size_t i, j; \
    row--;  /* row[-1] is implicit infinity */\
\
    vec_copy(row + 1, point, sizeof(ptype));    /* row[ 1]=p*1     */\
    ptype##_double(row + 2,  point);            /* row[ 2]=p*(1+1) */\
    for (i = 3, j = 2; i < 1<<(SZ-1); i += 2, j++) \
        ptype##_add(row+i, row+j, row+j-1),     /* row[ 3]=p*(2+1) */\
        ptype##_double(row+i+1, row+j);         /* row[ 4]=p*(2+2) */\
}                                               /* row[ 5] ...     */\
\
static void ptype##s_mult_w##SZ(ptype *ret, \
                                const ptype *points[], size_t npoints, \
                                const byte *scalars[], size_t bits, \
                                ptype table[][1<<(SZ-1)]) \
{ \
    limb_t wmask, wval; \
    size_t i, j, window; \
    ptype temp[1]; \
\
    if (table == NULL) \
        table = alloca((1<<(SZ-1)) * sizeof(ptype) * npoints); \
\
    for (i = 0; i < npoints; i++) \
        ptype##_precompute_w##SZ(table[i], points[i]); \
\
    /* top excess bits modulo target window size */ \
    window = bits % SZ; /* yes, it may be zero */ \
    wmask = ((limb_t)1 << (window + 1)) - 1; \
\
    bits -= window; \
    if (bits > 0) \
        wval = get_wval(scalars[0], bits - 1, window + 1) & wmask; \
    else \
        wval = (scalars[0][0] << 1) & wmask; \
\
    wval = booth_encode(wval, SZ); \
    ptype##_gather_booth_w##SZ(ret, table[0], wval); \
\
    i = 1; \
    while (bits > 0) { \
        for (; i < npoints; i++) { \
            wval = get_wval(scalars[i], bits - 1, window + 1) & wmask; \
            wval = booth_encode(wval, SZ); \
            ptype##_gather_booth_w##SZ(temp, table[i], wval); \
            ptype##_dadd(ret, ret, temp, NULL); \
        } \
\
        for (j = 0; j < SZ; j++) \
            ptype##_double(ret, ret); \
\
        window = SZ; \
        wmask = ((limb_t)1 << (window + 1)) - 1; \
        bits -= window; \
        i = 0; \
    } \
\
    for (; i < npoints; i++) { \
        wval = (scalars[i][0] << 1) & wmask; \
        wval = booth_encode(wval, SZ); \
        ptype##_gather_booth_w##SZ(temp, table[i], wval); \
        ptype##_dadd(ret, ret, temp, NULL); \
    } \
} \
\
static void ptype##_mult_w##SZ(ptype *ret, const ptype *point, \
                               const byte *scalar, size_t bits) \
{ \
    limb_t wmask, wval; \
    size_t j, window; \
    ptype temp[1]; \
    ptype table[1<<(SZ-1)]; \
\
    ptype##_precompute_w##SZ(table, point); \
\
    /* top excess bits modulo target window size */ \
    window = bits % SZ;  /* yes, it may be zero */ \
    wmask = ((limb_t)1 << (window + 1)) - 1; \
\
    bits -= window; \
    wval = bits ? get_wval(scalar, bits - 1, window + 1) \
                : (limb_t)scalar[0] << 1; \
    wval &= wmask; \
    wval = booth_encode(wval, SZ); \
    ptype##_gather_booth_w##SZ(ret, table, wval); \
\
    while (bits > 0) { \
        for (j = 0; j < SZ; j++) \
            ptype##_double(ret, ret); \
\
        window = SZ; \
        wmask = ((limb_t)1 << (window + 1)) - 1; \
        bits -= window; \
\
        wval = bits ? get_wval(scalar, bits - 1, window + 1) \
                    : (limb_t)scalar[0] << 1; \
        wval &= wmask; \
        wval = booth_encode(wval, SZ); \
        ptype##_gather_booth_w##SZ(temp, table, wval); \
        ptype##_add(ret, ret, temp); \
    } \
}

#if 0
/* ~50%, or ~2x[!] slower than w5... */
#define POINT_MULT_SCALAR_LADDER_IMPL(ptype) \
static void ptype##_mult_ladder(ptype *ret, const ptype *p, \
                                const byte *scalar, size_t bits) \
{ \
    ptype sum[1]; \
    limb_t bit, pbit = 0; \
\
    vec_copy(sum, p, sizeof(ptype)); \
    vec_zero(ret, sizeof(ptype));   /* infinity */ \
\
    while (bits--) { \
        bit = is_bit_set(scalar, bits); \
        bit ^= pbit; \
        ptype##_cswap(ret, sum, bit); \
        ptype##_add(sum, sum, ret); \
        ptype##_double(ret, ret); \
        pbit ^= bit; \
    } \
    ptype##_cswap(ret, sum, pbit); \
}
#else
/* >40% better performance than above, [and ~30% slower than w5]... */
#define POINT_MULT_SCALAR_LADDER_IMPL(ptype) \
static void ptype##_mult_ladder(ptype *out, const ptype *p, \
                                const byte *scalar, size_t bits) \
{ \
    ptype##xz sum[1]; \
    ptype##xz pxz[1]; \
    ptype##xz ret[1]; \
    limb_t bit, pbit = 0; \
\
    ptype##xz_ladder_pre(pxz, p); \
    vec_copy(sum, pxz, sizeof(ptype##xz)); \
    vec_zero(ret, sizeof(ptype##xz));   /* infinity */ \
\
    while (bits--) { \
        bit = is_bit_set(scalar, bits); \
        bit ^= pbit; \
        ptype##xz_cswap(ret, sum, bit); \
        ptype##xz_ladder_step(ret, sum, pxz); \
        pbit ^= bit; \
    } \
    ptype##xz_cswap(ret, sum, pbit); \
    ptype##xz_ladder_post(out, ret, sum, pxz, p->Y); \
}
#endif

/*
 * Sole reason for existence of this implementation is that addition
 * with affine point renders a share of multiplications redundant by
 * virtue of Z==1. And since pre-defined generator point can be and
 * customarily is instantiated affine, it would be hardly appropriate
 * to pass on this opportunity. Though while it's faster than the
 * generic ladder implementation, by ~25%, it's not faster than XZ one
 * above, <15% slower. Just in case, it's faster than generic ladder
 * even if one accounts for prior conversion to affine coordinates,
 * so that choice [for resource-constrained case] is actually between
 * this plus said conversion and XZ ladder...
 *
 * To summarize, if ptype##_mult_w5 executed in one unit of time, then
 * - naive ptype##_mult_ladder would execute in ~2;
 * - XZ version above - in ~1.4;
 * - ptype##_affine_mult_ladder below - in ~1.65;
 * - [small-footprint ptype##_to_affine would run in ~0.18].
 *
 * Caveat lector, |p_affine|*(order+2) produces wrong result, because
 * addition doesn't handle doubling. Indeed, P*(order+1) is P and it
 * fails to add with itself producing infinity in last addition. But
 * as long as |scalar| is reduced modulo order, as it should be, it's
 * not a problem...
 */
#define POINT_AFFINE_MULT_SCALAR_IMPL(ptype) \
static void ptype##_affine_mult_ladder(ptype *ret, const ptype *p_affine, \
                                       const byte *scalar, size_t bits) \
{ \
    ptype sum[1]; \
    limb_t bit; \
\
    vec_zero(ret, sizeof(ptype));   /* infinity */ \
\
    while (bits--) { \
        ptype##_double(ret, ret); \
        ptype##_add_affine(sum, ret, p_affine); \
        bit = (scalar[bits / LIMB_T_BITS] >> (bits % LIMB_T_BITS)) & 1; \
        ptype##_ccopy(ret, sum, bit); \
    } \
}
#endif
