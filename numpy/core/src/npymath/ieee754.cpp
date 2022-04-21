/* -*- c -*- */
/*
 * vim:syntax=c
 *
 * Low-level routines related to IEEE-754 format
 */
#include "numpy/utils.h"

#include "npy_math_common.h"
#include "npy_math_private.h"

#ifndef HAVE_COPYSIGN
double
npy_copysign(double x, double y)
{
    npy_uint32 hx, hy;
    GET_HIGH_WORD(hx, x);
    GET_HIGH_WORD(hy, y);
    SET_HIGH_WORD(x, (hx & 0x7fffffff) | (hy & 0x80000000));
    return x;
}
#endif

/*
 The below code is provided for compilers which do not yet provide C11
 compatibility (gcc 4.5 and older)
 */
#ifndef LDBL_TRUE_MIN
#define LDBL_TRUE_MIN __LDBL_DENORM_MIN__
#endif

#if !defined(HAVE_DECL_SIGNBIT)
#include "_signbit.c"

int
_npy_signbit_f(float x)
{
    return _npy_signbit_d((double)x);
}

int
_npy_signbit_ld(long double x)
{
    return _npy_signbit_d((double)x);
}
#endif

/*
 * FIXME: There is a lot of redundancy between _next* and npy_nextafter*.
 * refactor this at some point
 *
 * p >= 0, returnx x + nulp
 * p < 0, returnx x - nulp
 */
static double
_next(double x, int p)
{
    volatile double t;
    npy_int32 hx, hy, ix;
    npy_uint32 lx;

    EXTRACT_WORDS(hx, lx, x);
    ix = hx & 0x7fffffff; /* |x| */

    if (((ix >= 0x7ff00000) && ((ix - 0x7ff00000) | lx) != 0)) /* x is nan */
        return x;
    if ((ix | lx) == 0) { /* x == 0 */
        if (p >= 0) {
            INSERT_WORDS(x, 0x0, 1); /* return +minsubnormal */
        }
        else {
            INSERT_WORDS(x, 0x80000000, 1); /* return -minsubnormal */
        }
        t = x * x;
        if (t == x)
            return t;
        else
            return x; /* raise underflow flag */
    }
    if (p < 0) { /* x -= ulp */
        if (lx == 0)
            hx -= 1;
        lx -= 1;
    }
    else { /* x += ulp */
        lx += 1;
        if (lx == 0)
            hx += 1;
    }
    hy = hx & 0x7ff00000;
    if (hy >= 0x7ff00000)
        return x + x;      /* overflow  */
    if (hy < 0x00100000) { /* underflow */
        t = x * x;
        if (t != x) { /* raise underflow flag */
            INSERT_WORDS(x, hx, lx);
            return x;
        }
    }
    INSERT_WORDS(x, hx, lx);
    return x;
}

static float
_next(float x, int p)
{
    volatile float t;
    npy_int32 hx, hy, ix;

    GET_FLOAT_WORD(hx, x);
    ix = hx & 0x7fffffff; /* |x| */

    if ((ix > 0x7f800000)) /* x is nan */
        return x;
    if (ix == 0) { /* x == 0 */
        if (p >= 0) {
            SET_FLOAT_WORD(x, 0x0 | 1); /* return +minsubnormal */
        }
        else {
            SET_FLOAT_WORD(x, 0x80000000 | 1); /* return -minsubnormal */
        }
        t = x * x;
        if (t == x)
            return t;
        else
            return x; /* raise underflow flag */
    }
    if (p < 0) { /* x -= ulp */
        hx -= 1;
    }
    else { /* x += ulp */
        hx += 1;
    }
    hy = hx & 0x7f800000;
    if (hy >= 0x7f800000)
        return x + x;      /* overflow  */
    if (hy < 0x00800000) { /* underflow */
        t = x * x;
        if (t != x) { /* raise underflow flag */
            SET_FLOAT_WORD(x, hx);
            return x;
        }
    }
    SET_FLOAT_WORD(x, hx);
    return x;
}

#if defined(HAVE_LDOUBLE_DOUBLE_DOUBLE_BE) || \
        defined(HAVE_LDOUBLE_DOUBLE_DOUBLE_LE)

/*
 * FIXME: this is ugly and untested. The asm part only works with gcc, and we
 * should consolidate the GET_LDOUBLE* / SET_LDOUBLE macros
 */
#define math_opt_barrier(x)    \
    ({                         \
        __typeof(x) __x = x;   \
        __asm("" : "+m"(__x)); \
        __x;                   \
    })
#define math_force_eval(x) __asm __volatile("" : : "m"(x))

/* only works for big endian */
typedef union {
    npy_longdouble value;
    struct {
        npy_uint64 msw;
        npy_uint64 lsw;
    } parts64;
    struct {
        npy_uint32 w0, w1, w2, w3;
    } parts32;
} ieee854_long_double_shape_type;

/* Get two 64 bit ints from a long double.  */

#define GET_LDOUBLE_WORDS64(ix0, ix1, d)     \
    do {                                     \
        ieee854_long_double_shape_type qw_u; \
        qw_u.value = (d);                    \
        (ix0) = qw_u.parts64.msw;            \
        (ix1) = qw_u.parts64.lsw;            \
    } while (0)

/* Set a long double from two 64 bit ints.  */

#define SET_LDOUBLE_WORDS64(d, ix0, ix1)     \
    do {                                     \
        ieee854_long_double_shape_type qw_u; \
        qw_u.parts64.msw = (ix0);            \
        qw_u.parts64.lsw = (ix1);            \
        (d) = qw_u.value;                    \
    } while (0)

static long double
_next(long double x, int p)
{
    npy_int64 hx, ihx, ilx;
    npy_uint64 lx;
    npy_longdouble u;
    const npy_longdouble eps = exp2l(-105.); // 0x1.0000000000000p-105L

    GET_LDOUBLE_WORDS64(hx, lx, x);
    ihx = hx & 0x7fffffffffffffffLL; /* |hx| */
    ilx = lx & 0x7fffffffffffffffLL; /* |lx| */

    if (((ihx & 0x7ff0000000000000LL) == 0x7ff0000000000000LL) &&
        ((ihx & 0x000fffffffffffffLL) != 0)) {
        return x; /* signal the nan */
    }
    if (ihx == 0 && ilx == 0) {          /* x == 0 */
        SET_LDOUBLE_WORDS64(x, p, 0ULL); /* return +-minsubnormal */
        u = x * x;
        if (u == x) {
            return u;
        }
        else {
            return x; /* raise underflow flag */
        }
    }

    if (p < 0) { /* p < 0, x -= ulp */
        if ((hx == 0xffefffffffffffffLL) && (lx == 0xfc8ffffffffffffeLL))
            return x + x; /* overflow, return -inf */
        if (hx >= 0x7ff0000000000000LL) {
            SET_LDOUBLE_WORDS64(u, 0x7fefffffffffffffLL, 0x7c8ffffffffffffeLL);
            return u;
        }
        if (ihx <= 0x0360000000000000LL) { /* x <= LDBL_MIN */
            u = math_opt_barrier(x);
            x -= LDBL_TRUE_MIN;
            if (ihx < 0x0360000000000000LL || (hx > 0 && (npy_int64)lx <= 0) ||
                (hx < 0 && (npy_int64)lx > 1)) {
                u = u * u;
                math_force_eval(u); /* raise underflow flag */
            }
            return x;
        }
        if (ihx < 0x06a0000000000000LL) { /* ulp will denormal */
            SET_LDOUBLE_WORDS64(u, (hx & 0x7ff0000000000000LL), 0ULL);
            u *= eps;
        }
        else
            SET_LDOUBLE_WORDS64(
                    u, (hx & 0x7ff0000000000000LL) - 0x0690000000000000LL,
                    0ULL);
        return x - u;
    }
    else { /* p >= 0, x += ulp */
        if ((hx == 0x7fefffffffffffffLL) && (lx == 0x7c8ffffffffffffeLL))
            return x + x; /* overflow, return +inf */
        if ((npy_uint64)hx >= 0xfff0000000000000ULL) {
            SET_LDOUBLE_WORDS64(u, 0xffefffffffffffffLL, 0xfc8ffffffffffffeLL);
            return u;
        }
        if (ihx <= 0x0360000000000000LL) { /* x <= LDBL_MIN */
            u = math_opt_barrier(x);
            x += LDBL_TRUE_MIN;
            if (ihx < 0x0360000000000000LL ||
                (hx > 0 && (npy_int64)lx < 0 && lx != 0x8000000000000001LL) ||
                (hx < 0 && (npy_int64)lx >= 0)) {
                u = u * u;
                math_force_eval(u); /* raise underflow flag */
            }
            if (x == 0.0L) /* handle negative LDBL_TRUE_MIN case */
                x = -0.0L;
            return x;
        }
        if (ihx < 0x06a0000000000000LL) { /* ulp will denormal */
            SET_LDOUBLE_WORDS64(u, (hx & 0x7ff0000000000000LL), 0ULL);
            u *= eps;
        }
        else
            SET_LDOUBLE_WORDS64(
                    u, (hx & 0x7ff0000000000000LL) - 0x0690000000000000LL,
                    0ULL);
        return x + u;
    }
}
#else
static long double
_next(long double x, int p)
{
    volatile npy_longdouble t;
    union IEEEl2bitsrep ux;

    ux.e = x;

    if ((GET_LDOUBLE_EXP(ux) == 0x7fff &&
         ((GET_LDOUBLE_MANH(ux) & ~LDBL_NBIT) | GET_LDOUBLE_MANL(ux)) != 0)) {
        return ux.e; /* x is nan */
    }
    if (ux.e == 0.0) {
        SET_LDOUBLE_MANH(ux, 0); /* return +-minsubnormal */
        SET_LDOUBLE_MANL(ux, 1);
        if (p >= 0) {
            SET_LDOUBLE_SIGN(ux, 0);
        }
        else {
            SET_LDOUBLE_SIGN(ux, 1);
        }
        t = ux.e * ux.e;
        if (t == ux.e) {
            return t;
        }
        else {
            return ux.e; /* raise underflow flag */
        }
    }
    if (p < 0) { /* x -= ulp */
        if (GET_LDOUBLE_MANL(ux) == 0) {
            if ((GET_LDOUBLE_MANH(ux) & ~LDBL_NBIT) == 0) {
                SET_LDOUBLE_EXP(ux, GET_LDOUBLE_EXP(ux) - 1);
            }
            SET_LDOUBLE_MANH(ux, (GET_LDOUBLE_MANH(ux) - 1) |
                                         (GET_LDOUBLE_MANH(ux) & LDBL_NBIT));
        }
        SET_LDOUBLE_MANL(ux, GET_LDOUBLE_MANL(ux) - 1);
    }
    else { /* x += ulp */
        SET_LDOUBLE_MANL(ux, GET_LDOUBLE_MANL(ux) + 1);
        if (GET_LDOUBLE_MANL(ux) == 0) {
            SET_LDOUBLE_MANH(ux, (GET_LDOUBLE_MANH(ux) + 1) |
                                         (GET_LDOUBLE_MANH(ux) & LDBL_NBIT));
            if ((GET_LDOUBLE_MANH(ux) & ~LDBL_NBIT) == 0) {
                SET_LDOUBLE_EXP(ux, GET_LDOUBLE_EXP(ux) + 1);
            }
        }
    }
    if (GET_LDOUBLE_EXP(ux) == 0x7fff) {
        return ux.e + ux.e; /* overflow  */
    }
    if (GET_LDOUBLE_EXP(ux) == 0) { /* underflow */
        if (LDBL_NBIT) {
            SET_LDOUBLE_MANH(ux, GET_LDOUBLE_MANH(ux) & ~LDBL_NBIT);
        }
        t = ux.e * ux.e;
        if (t != ux.e) { /* raise underflow flag */
            return ux.e;
        }
    }

    return ux.e;
}
#endif

/*
 * nextafter code taken from BSD math lib, the code contains the following
 * notice:
 *
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */

#ifndef HAVE_NEXTAFTER
double
npy_nextafter(double x, double y)
{
    volatile double t;
    npy_int32 hx, hy, ix, iy;
    npy_uint32 lx, ly;

    EXTRACT_WORDS(hx, lx, x);
    EXTRACT_WORDS(hy, ly, y);
    ix = hx & 0x7fffffff; /* |x| */
    iy = hy & 0x7fffffff; /* |y| */

    if (((ix >= 0x7ff00000) && ((ix - 0x7ff00000) | lx) != 0) || /* x is nan */
        ((iy >= 0x7ff00000) && ((iy - 0x7ff00000) | ly) != 0))   /* y is nan */
        return x + y;
    if (x == y)
        return y;                            /* x=y, return y */
    if ((ix | lx) == 0) {                    /* x == 0 */
        INSERT_WORDS(x, hy & 0x80000000, 1); /* return +-minsubnormal */
        t = x * x;
        if (t == x)
            return t;
        else
            return x; /* raise underflow flag */
    }
    if (hx >= 0) {                                  /* x > 0 */
        if (hx > hy || ((hx == hy) && (lx > ly))) { /* x > y, x -= ulp */
            if (lx == 0)
                hx -= 1;
            lx -= 1;
        }
        else { /* x < y, x += ulp */
            lx += 1;
            if (lx == 0)
                hx += 1;
        }
    }
    else { /* x < 0 */
        if (hy >= 0 || hx > hy ||
            ((hx == hy) && (lx > ly))) { /* x < y, x -= ulp */
            if (lx == 0)
                hx -= 1;
            lx -= 1;
        }
        else { /* x > y, x += ulp */
            lx += 1;
            if (lx == 0)
                hx += 1;
        }
    }
    hy = hx & 0x7ff00000;
    if (hy >= 0x7ff00000)
        return x + x;      /* overflow  */
    if (hy < 0x00100000) { /* underflow */
        t = x * x;
        if (t != x) { /* raise underflow flag */
            INSERT_WORDS(y, hx, lx);
            return y;
        }
    }
    INSERT_WORDS(x, hx, lx);
    return x;
}
#endif

#ifndef HAVE_NEXTAFTERF
float
npy_nextafterf(float x, float y)
{
    volatile float t;
    npy_int32 hx, hy, ix, iy;

    GET_FLOAT_WORD(hx, x);
    GET_FLOAT_WORD(hy, y);
    ix = hx & 0x7fffffff; /* |x| */
    iy = hy & 0x7fffffff; /* |y| */

    if ((ix > 0x7f800000) || /* x is nan */
        (iy > 0x7f800000))   /* y is nan */
        return x + y;
    if (x == y)
        return y;                                 /* x=y, return y */
    if (ix == 0) {                                /* x == 0 */
        SET_FLOAT_WORD(x, (hy & 0x80000000) | 1); /* return +-minsubnormal */
        t = x * x;
        if (t == x)
            return t;
        else
            return x; /* raise underflow flag */
    }
    if (hx >= 0) {     /* x > 0 */
        if (hx > hy) { /* x > y, x -= ulp */
            hx -= 1;
        }
        else { /* x < y, x += ulp */
            hx += 1;
        }
    }
    else {                        /* x < 0 */
        if (hy >= 0 || hx > hy) { /* x < y, x -= ulp */
            hx -= 1;
        }
        else { /* x > y, x += ulp */
            hx += 1;
        }
    }
    hy = hx & 0x7f800000;
    if (hy >= 0x7f800000)
        return x + x;      /* overflow  */
    if (hy < 0x00800000) { /* underflow */
        t = x * x;
        if (t != x) { /* raise underflow flag */
            SET_FLOAT_WORD(y, hx);
            return y;
        }
    }
    SET_FLOAT_WORD(x, hx);
    return x;
}
#endif

#ifndef HAVE_NEXTAFTERL
npy_longdouble
npy_nextafterl(npy_longdouble x, npy_longdouble y)
{
    volatile npy_longdouble t;
    union IEEEl2bitsrep ux;
    union IEEEl2bitsrep uy;

    ux.e = x;
    uy.e = y;

    if ((GET_LDOUBLE_EXP(ux) == 0x7fff &&
         ((GET_LDOUBLE_MANH(ux) & ~LDBL_NBIT) | GET_LDOUBLE_MANL(ux)) != 0) ||
        (GET_LDOUBLE_EXP(uy) == 0x7fff &&
         ((GET_LDOUBLE_MANH(uy) & ~LDBL_NBIT) | GET_LDOUBLE_MANL(uy)) != 0)) {
        return ux.e + uy.e; /* x or y is nan */
    }
    if (ux.e == uy.e) {
        return uy.e; /* x=y, return y */
    }
    if (ux.e == 0.0) {
        SET_LDOUBLE_MANH(ux, 0); /* return +-minsubnormal */
        SET_LDOUBLE_MANL(ux, 1);
        SET_LDOUBLE_SIGN(ux, GET_LDOUBLE_SIGN(uy));
        t = ux.e * ux.e;
        if (t == ux.e) {
            return t;
        }
        else {
            return ux.e; /* raise underflow flag */
        }
    }
    if ((ux.e > 0.0) ^ (ux.e < uy.e)) { /* x -= ulp */
        if (GET_LDOUBLE_MANL(ux) == 0) {
            if ((GET_LDOUBLE_MANH(ux) & ~LDBL_NBIT) == 0) {
                SET_LDOUBLE_EXP(ux, GET_LDOUBLE_EXP(ux) - 1);
            }
            SET_LDOUBLE_MANH(ux, (GET_LDOUBLE_MANH(ux) - 1) |
                                         (GET_LDOUBLE_MANH(ux) & LDBL_NBIT));
        }
        SET_LDOUBLE_MANL(ux, GET_LDOUBLE_MANL(ux) - 1);
    }
    else { /* x += ulp */
        SET_LDOUBLE_MANL(ux, GET_LDOUBLE_MANL(ux) + 1);
        if (GET_LDOUBLE_MANL(ux) == 0) {
            SET_LDOUBLE_MANH(ux, (GET_LDOUBLE_MANH(ux) + 1) |
                                         (GET_LDOUBLE_MANH(ux) & LDBL_NBIT));
            if ((GET_LDOUBLE_MANH(ux) & ~LDBL_NBIT) == 0) {
                SET_LDOUBLE_EXP(ux, GET_LDOUBLE_EXP(ux) + 1);
            }
        }
    }
    if (GET_LDOUBLE_EXP(ux) == 0x7fff) {
        return ux.e + ux.e; /* overflow  */
    }
    if (GET_LDOUBLE_EXP(ux) == 0) { /* underflow */
        if (LDBL_NBIT) {
            SET_LDOUBLE_MANH(ux, GET_LDOUBLE_MANH(ux) & ~LDBL_NBIT);
        }
        t = ux.e * ux.e;
        if (t != ux.e) { /* raise underflow flag */
            return ux.e;
        }
    }

    return ux.e;
}
#endif

namespace {
template <typename T>
struct numeric_limits;

template <>
struct numeric_limits<float> {
    static const npy_float nan;
};
const npy_float numeric_limits<float>::nan = NPY_NANF;

template <>
struct numeric_limits<double> {
    static const npy_double nan;
};
const npy_double numeric_limits<double>::nan = NPY_NAN;

template <>
struct numeric_limits<long double> {
    static const npy_longdouble nan;
};
const npy_longdouble numeric_limits<long double>::nan = NPY_NANL;
}  // namespace

template <typename type>
static type
_npy_spacing(type x)
{
    /* XXX: npy isnan/isinf may be optimized by bit twiddling */
    if (npy_isinf(x)) {
        return numeric_limits<type>::nan;
    }

    return _next(x, 1) - x;
}

/*
 * Instantiation of C interface
 */
extern "C" {
npy_float
npy_spacingf(npy_float x)
{
    return _npy_spacing(x);
}
npy_double
npy_spacing(npy_double x)
{
    return _npy_spacing(x);
}
npy_longdouble
npy_spacingl(npy_longdouble x)
{
    return _npy_spacing(x);
}
}

/*
 * Decorate all the math functions which are available on the current platform
 */

#ifdef HAVE_NEXTAFTERF
extern "C" float
npy_nextafterf(float x, float y)
{
    return nextafterf(x, y);
}
#endif

#ifdef HAVE_NEXTAFTER
extern "C" double
npy_nextafter(double x, double y)
{
    return nextafter(x, y);
}
#endif

#ifdef HAVE_NEXTAFTERL
extern "C" npy_longdouble
npy_nextafterl(npy_longdouble x, npy_longdouble y)
{
    return nextafterl(x, y);
}
#endif

extern "C" int
npy_clear_floatstatus()
{
    char x = 0;
    return npy_clear_floatstatus_barrier(&x);
}
extern "C" int
npy_get_floatstatus()
{
    char x = 0;
    return npy_get_floatstatus_barrier(&x);
}

/*
 * Functions to set the floating point status word.
 */

#if (defined(__unix__) || defined(unix)) && !defined(USG)
#include <sys/param.h>
#endif

/*
 * Define floating point status functions. We must define
 * npy_get_floatstatus_barrier, npy_clear_floatstatus_barrier,
 * npy_set_floatstatus_{divbyzero, overflow, underflow, invalid}
 * for all supported platforms.
 */

/* Solaris --------------------------------------------------------*/
/* --------ignoring SunOS ieee_flags approach, someone else can
**         deal with that! */
#if defined(sun) || defined(__BSD__) || defined(__OpenBSD__) ||   \
        (defined(__FreeBSD__) && (__FreeBSD_version < 502114)) || \
        defined(__NetBSD__)
#include <ieeefp.h>

extern "C" int
npy_get_floatstatus_barrier(char *param)
{
    int fpstatus = fpgetsticky();
    /*
     * By using a volatile, the compiler cannot reorder this call
     */
    if (param != NULL) {
        volatile char NPY_UNUSED(c) = *(char *)param;
    }
    return ((FP_X_DZ & fpstatus) ? NPY_FPE_DIVIDEBYZERO : 0) |
           ((FP_X_OFL & fpstatus) ? NPY_FPE_OVERFLOW : 0) |
           ((FP_X_UFL & fpstatus) ? NPY_FPE_UNDERFLOW : 0) |
           ((FP_X_INV & fpstatus) ? NPY_FPE_INVALID : 0);
}

extern "C" int
npy_clear_floatstatus_barrier(char *param)
{
    int fpstatus = npy_get_floatstatus_barrier(param);
    fpsetsticky(0);

    return fpstatus;
}

extern "C" void
npy_set_floatstatus_divbyzero(void)
{
    fpsetsticky(FP_X_DZ);
}

extern "C" void
npy_set_floatstatus_overflow(void)
{
    fpsetsticky(FP_X_OFL);
}

extern "C" void
npy_set_floatstatus_underflow(void)
{
    fpsetsticky(FP_X_UFL);
}

extern "C" void
npy_set_floatstatus_invalid(void)
{
    fpsetsticky(FP_X_INV);
}

#elif defined(_AIX) && !defined(__GNUC__)
#include <cfloat>
#include <fpxcp.h>

extern "C" int
npy_get_floatstatus_barrier(char *param)
{
    int fpstatus = fp_read_flag();
    /*
     * By using a volatile, the compiler cannot reorder this call
     */
    if (param != NULL) {
        volatile char NPY_UNUSED(c) = *(char *)param;
    }
    return ((FP_DIV_BY_ZERO & fpstatus) ? NPY_FPE_DIVIDEBYZERO : 0) |
           ((FP_OVERFLOW & fpstatus) ? NPY_FPE_OVERFLOW : 0) |
           ((FP_UNDERFLOW & fpstatus) ? NPY_FPE_UNDERFLOW : 0) |
           ((FP_INVALID & fpstatus) ? NPY_FPE_INVALID : 0);
}

extern "C" int
npy_clear_floatstatus_barrier(char *param)
{
    int fpstatus = npy_get_floatstatus_barrier(param);
    fp_swap_flag(0);

    return fpstatus;
}

extern "C" void
npy_set_floatstatus_divbyzero(void)
{
    fp_raise_xcp(FP_DIV_BY_ZERO);
}

extern "C" void
npy_set_floatstatus_overflow(void)
{
    fp_raise_xcp(FP_OVERFLOW);
}

extern "C" void
npy_set_floatstatus_underflow(void)
{
    fp_raise_xcp(FP_UNDERFLOW);
}

extern "C" void
npy_set_floatstatus_invalid(void)
{
    fp_raise_xcp(FP_INVALID);
}

#elif defined(_MSC_VER) || (defined(__osf__) && defined(__alpha)) || \
        defined(__UCLIBC__) || (defined(__arc__) && defined(__GLIBC__))

/*
 * By using a volatile floating point value,
 * the compiler is forced to actually do the requested
 * operations because of potential concurrency.
 *
 * We shouldn't write multiple values to a single
 * global here, because that would cause
 * a race condition.
 */
static volatile double _npy_floatstatus_x,
        _npy_floatstatus_zero = 0.0, _npy_floatstatus_big = 1e300,
        _npy_floatstatus_small = 1e-300, _npy_floatstatus_inf;

extern "C" void
npy_set_floatstatus_divbyzero(void)
{
    _npy_floatstatus_x = 1.0 / _npy_floatstatus_zero;
}

extern "C" void
npy_set_floatstatus_overflow(void)
{
    _npy_floatstatus_x = _npy_floatstatus_big * 1e300;
}

extern "C" void
npy_set_floatstatus_underflow(void)
{
    _npy_floatstatus_x = _npy_floatstatus_small * 1e-300;
}

extern "C" void
npy_set_floatstatus_invalid(void)
{
    _npy_floatstatus_inf = NPY_INFINITY;
    _npy_floatstatus_x = _npy_floatstatus_inf - NPY_INFINITY;
}

/* MS Windows -----------------------------------------------------*/
#if defined(_MSC_VER)

#include <cfloat>

extern "C" int
npy_get_floatstatus_barrier(char *param)
{
    /*
     * By using a volatile, the compiler cannot reorder this call
     */
#if defined(_WIN64)
    int fpstatus = _statusfp();
#else
    /* windows enables sse on 32 bit, so check both flags */
    unsigned int fpstatus, fpstatus2;
    _statusfp2(&fpstatus, &fpstatus2);
    fpstatus |= fpstatus2;
#endif
    if (param != NULL) {
        volatile char NPY_UNUSED(c) = *(char *)param;
    }
    return ((SW_ZERODIVIDE & fpstatus) ? NPY_FPE_DIVIDEBYZERO : 0) |
           ((SW_OVERFLOW & fpstatus) ? NPY_FPE_OVERFLOW : 0) |
           ((SW_UNDERFLOW & fpstatus) ? NPY_FPE_UNDERFLOW : 0) |
           ((SW_INVALID & fpstatus) ? NPY_FPE_INVALID : 0);
}

extern "C" int
npy_clear_floatstatus_barrier(char *param)
{
    int fpstatus = npy_get_floatstatus_barrier(param);
    _clearfp();

    return fpstatus;
}

/*  OSF/Alpha (Tru64)  ---------------------------------------------*/
#elif defined(__osf__) && defined(__alpha)

#include <machine/fpu.h>

extern "C" int
npy_get_floatstatus_barrier(char *param)
{
    unsigned long fpstatus = ieee_get_fp_control();
    /*
     * By using a volatile, the compiler cannot reorder this call
     */
    if (param != NULL) {
        volatile char NPY_UNUSED(c) = *(char *)param;
    }
    return ((IEEE_STATUS_DZE & fpstatus) ? NPY_FPE_DIVIDEBYZERO : 0) |
           ((IEEE_STATUS_OVF & fpstatus) ? NPY_FPE_OVERFLOW : 0) |
           ((IEEE_STATUS_UNF & fpstatus) ? NPY_FPE_UNDERFLOW : 0) |
           ((IEEE_STATUS_INV & fpstatus) ? NPY_FPE_INVALID : 0);
}

extern "C" int
npy_clear_floatstatus_barrier(char *param)
{
    int fpstatus = npy_get_floatstatus_barrier(param);
    /* clear status bits as well as disable exception mode if on */
    ieee_set_fp_control(0);

    return fpstatus;
}

#endif
/* End of defined(_MSC_VER) || (defined(__osf__) && defined(__alpha)) */

#else
/* General GCC code, should work on most platforms */
#include <fenv.h>

extern "C" int
npy_get_floatstatus_barrier(char *param)
{
    int fpstatus = fetestexcept(FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW |
                                FE_INVALID);
    /*
     * By using a volatile, the compiler cannot reorder this call
     */
    if (param != NULL) {
        volatile char NPY_UNUSED(c) = *(char *)param;
    }

    return ((FE_DIVBYZERO & fpstatus) ? NPY_FPE_DIVIDEBYZERO : 0) |
           ((FE_OVERFLOW & fpstatus) ? NPY_FPE_OVERFLOW : 0) |
           ((FE_UNDERFLOW & fpstatus) ? NPY_FPE_UNDERFLOW : 0) |
           ((FE_INVALID & fpstatus) ? NPY_FPE_INVALID : 0);
}

extern "C" int
npy_clear_floatstatus_barrier(char *param)
{
    /* testing float status is 50-100 times faster than clearing on x86 */
    int fpstatus = npy_get_floatstatus_barrier(param);
    if (fpstatus != 0) {
        feclearexcept(FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW | FE_INVALID);
    }

    return fpstatus;
}

extern "C" void
npy_set_floatstatus_divbyzero(void)
{
    feraiseexcept(FE_DIVBYZERO);
}

extern "C" void
npy_set_floatstatus_overflow(void)
{
    feraiseexcept(FE_OVERFLOW);
}

extern "C" void
npy_set_floatstatus_underflow(void)
{
    feraiseexcept(FE_UNDERFLOW);
}

extern "C" void
npy_set_floatstatus_invalid(void)
{
    feraiseexcept(FE_INVALID);
}

#endif
