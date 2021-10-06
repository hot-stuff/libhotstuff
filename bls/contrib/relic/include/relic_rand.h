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
 * @defgroup rand Pseudo-random number generators.
 */

/**
 * @file
 *
 * Interface of the module for pseudo-random number generation.
 *
 * @ingroup rand
 */

#ifndef RLC_RAND_H
#define RLC_RAND_H

#include "relic_rand.h"

/*============================================================================*/
/* Constant definitions                                                       */
/*============================================================================*/

/**
 * Size of the PRNG internal state in bytes.
 */
#if RAND == HASHD

#if MD_MAP == SH224 || MD_MAP == SH256 || MD_MAP == BLAKE2S_160 || MD_MAP == BLAKE2S_256
#define RAND_SIZE		(1 + 2*440/8)
#elif MD_MAP == SH384 || MD_MAP == SH512
#define RAND_SIZE		(1 + 2*888/8)
#endif

#elif RAND == UDEV
#define RAND_SIZE		(sizeof(int))
#elif RAND == CALL
#define RAND_SIZE		(sizeof(void (*)(uint8_t *, int)))
#elif RAND == RDRND
#define RAND_SIZE      0
#endif

/**
 * Minimum size of the PRNG seed.
 */
#define SEED_SIZE	    64

/*============================================================================*/
/* Function prototypes                                                        */
/*============================================================================*/

/**
 * Initializes the pseudo-random number generator.
 */
void rand_init(void);

/**
 * Finishes the pseudo-random number generator.
 */
void rand_clean(void);

#if RAND != CALL

/**
 * Sets the initial state of the pseudo-random number generator.
 *
 * @param[in] buf			- the buffer that represents the initial state.
 * @param[in] size			- the number of bytes.
 * @throw ERR_NO_VALID		- if the entropy length is too small or too large.
 */
void rand_seed(uint8_t *buf, int size);

#else

/**
 * Sets the initial state of the pseudo-random number generator as a function
 * pointer.
 *
 * @param[in] callback		- the callback to call.
 * @param[in] arg			- the argument for the callback.
 */
void rand_seed(void (*callback)(uint8_t *, int, void *), void *arg);

#endif

/**
 * Gathers pseudo-random bytes from the pseudo-random number generator.
 *
 * @param[out] buf			- the buffer to write.
 * @param[in] size			- the number of bytes to gather.
 * @throw ERR_NO_VALID		- if the required length is too large.
 * @throw ERR_NO_READ		- it the pseudo-random number generator cannot
 * 							generate the specified number of bytes.
 */
void rand_bytes(uint8_t *buf, int size);

#endif /* !RLC_RAND_H */
