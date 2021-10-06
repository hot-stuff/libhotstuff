/*
 * RELIC is an Efficient LIbrary for Cryptography
 * Copyright (C) 2007-2019 RELIC Authors
 *
 * This file is part of RELIC. RELIC is legal property of its developers,
 * whose names are not listed here. Please refer to the COPYRIGHT file
 * for contact information.
 *
 * RELIC is free software; you can redistribute it and/or modify it under the
 * terms of the version 2.1 (or later) of the GNU Lesser General Public License
 * as published by the Free Software Foundation; or version 2.0 of the Apache
 * License as published by the Apache Software Foundation. See the LICENSE files
 * for more details.
 *
 * RELIC is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the LICENSE files for more details.
 *
 * You should have received a copy of the GNU Lesser General Public or the
 * Apache License along with RELIC. If not, see <https://www.gnu.org/licenses/>
 * or <https://www.apache.org/licenses/>.
 */

/**
 * @file
 *
 * Elementary types.
 *
 * @ingroup relic
 */

#ifndef RLC_TYPES_H
#define RLC_TYPES_H

#include <stdint.h>

#include "relic_conf.h"

#if defined GMP && ARITH == GMP
#include <gmp.h>
#endif

/*============================================================================*/
/* Constant definitions                                                       */
/*============================================================================*/

/**
 * Size in bits of a digit.
 */
#define RLC_DIG			(WSIZE)

/**
 * Logarithm of the digit size in bits in base two.
 */
#if RLC_DIG == 8
#define RLC_DIG_LOG		3
#elif RLC_DIG == 16
#define RLC_DIG_LOG		4
#elif RLC_DIG == 32
#define RLC_DIG_LOG		5
#elif RLC_DIG == 64
#define RLC_DIG_LOG		6
#endif

/*============================================================================*/
/* Type definitions                                                           */
/*============================================================================*/

/**
 * Represents a digit from a multiple precision integer.
 *
 * Each digit is represented as an unsigned long to use the biggest native
 * type that potentially supports native instructions.
 */
#if ARITH == GMP
typedef mp_limb_t dig_t;
#elif WSIZE == 8
typedef uint8_t dig_t;
#elif WSIZE == 16
typedef uint16_t dig_t;
#elif WSIZE == 32
typedef uint32_t dig_t;
#elif WSIZE == 64
typedef uint64_t dig_t;
#endif

/**
 * Represents a signed digit.
 */
#if WSIZE == 8
typedef int8_t dis_t;
#elif WSIZE == 16
typedef int16_t dis_t;
#elif WSIZE == 32
typedef int32_t dis_t;
#elif WSIZE == 64
typedef int64_t dis_t;
#endif

/**
 * Represents a double-precision integer from a multiple precision integer.
 *
 * This is useful to store a result from a multiplication of two digits.
 */
#if WSIZE == 8
typedef uint16_t dbl_t;
#elif WSIZE == 16
typedef uint32_t dbl_t;
#elif WSIZE == 32
typedef uint64_t dbl_t;
#elif WSIZE == 64
#if defined(__GNUC__) && !defined(__INTEL_COMPILER)
typedef __uint128_t dbl_t;
#elif ARITH == EASY
#error "Easy backend in 64-bit mode supported only in GCC compiler."
#else
#endif
#endif

/*
 * Represents the unsigned integer with maximum precision.
 */
typedef unsigned long long ull_t;

/*============================================================================*/
/* Macro definitions                                                          */
/*============================================================================*/

/**
 * Specification for aligned variables.
 */
#if ALIGN > 1
#define rlc_align 		__attribute__ ((aligned (ALIGN)))
#else
#define rlc_align 		/* empty*/
#endif

/**
 * Size of padding to be added so that digit vectors are aligned.
 */
#if ALIGN > 1
#define RLC_PAD(A)		((A) % ALIGN == 0 ? 0 : ALIGN - ((A) % ALIGN))
#else
#define RLC_PAD(A)		(0)
#endif

/**
 * Align digit vector pointer to specified byte-boundary.
 *
 * @param[in,out] A		- the pointer to align.
 */
#if ALIGN > 1
#if ARCH == AVR || ARCH == MSP || ARCH == X86 || ARCH == ARM
#define RLC_ALIGN(A)														\
	((unsigned int)(A) + RLC_PAD((unsigned int)(A)));						\

#elif ARCH  == X64
#define RLC_ALIGN(A)														\
	((unsigned long)(A) + RLC_PAD((unsigned long)(A)));						\

#endif
#else
#define RLC_ALIGN(A)		(A)
#endif

#endif /* !RLC_TYPES_H */
