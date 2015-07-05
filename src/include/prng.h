
/* dist: public */

#ifndef __PRNG_H
#define __PRNG_H

/** @file
 * this interface services requests for pseudo-random numbers.
 * it's thread safe and nonbiased and all that good stuff.
 */

/** the Iprng interface id */
#define I_PRNG "prng-2"

/** the Iprng interface struct */
typedef struct Iprng
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	/** Fills a buffer with really secure random bits.
	 * Use this for cryptographic purposes and not much else. Uses
	 * /dev/urandom on unix and some cryptoapi stuff on windows.
	 * @param buf the buffer to fill
	 * @param size how many bytes to fill
	 * @return true on success
	 */
	int (*GoodFillBuffer)(void *buf, int size);

	/** Fills a buffer with decent random bits.
	 * @param buf the buffer to fill
	 * @param size how many bytes to fill
	 * @return true on success
	 */
	int (*FillBuffer)(void *buf, int size);

	/** Gets a random number between two inclusive bounds.
	 * @param start the lower bound
	 * @param end the upper bound
	 * @return a random number in [start, end]
	 */
	int (*Number)(int start, int end);
	/* pyint: int, int -> int */

	/** Gets a random 32-bit integer.
	 * @return a random 32-bit unsigned integer
	 */
	u32 (*Get32)(void);

	/** Gets a number from 0 to RAND_MAX.
	 * Use this anywhere you would have used rand() or random().
	 * @return a random integer in [0, RAND_MAX] */
	int (*Rand)(void);
	/* pyint: void -> int */

	/** Gets a floating point value uniformly distributed from 0 to 1.
	 * @return a floating point value in [0.0, 1.0)
	 */
	double (*Uniform)(void);
	/* pyint: void -> double */
} Iprng;

#endif
