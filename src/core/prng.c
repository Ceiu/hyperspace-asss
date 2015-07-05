
/* dist: public */

#ifndef WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "asss.h"

#include "md5.h"

local pthread_mutex_t prng_mtx = PTHREAD_MUTEX_INITIALIZER;

#define MUTEX_LOCK() pthread_mutex_lock(&prng_mtx)
#define MUTEX_UNLOCK() pthread_mutex_unlock(&prng_mtx)

local int iteration_counter;

/*** RNG -- Random Number Generator ***/

#ifdef WIN32
	/* Fix Microsoft Visual Studio 6 header file bug.
	   Should also compile using VS.NET which does not have this problem. */
	#ifdef __WINCRYPT_H__
		#if(_WIN32_WINNT < 0x0400)
			#undef __WINCRYPT_H__
			#define _WIN32_WINNT 0x0400
			#include <Wincrypt.h>
		#endif
	#else
		#if(_WIN32_WINNT < 0x0400)
			#define _WIN32_WINNT 0x0400
		#endif
		#include <Wincrypt.h>
	#endif

local double microseconds()
{
	LARGE_INTEGER tim, freq; /* 64-bit! ieee */

	QueryPerformanceCounter(&tim);
	QueryPerformanceFrequency(&freq);
	return ((double)(tim.QuadPart) * 1000000.0) / (double)(freq.QuadPart);
}

#endif

local int RNG_FillBuffer(void *buffer, int size)
{
#ifdef WIN32
	/* added some extra variables just in case */
	HCRYPTPROV hCryptProv;

	if (!buffer || size <= 0) return 0;

	MUTEX_LOCK();
	if (size >= 4)
		*(u32*)buffer += (u32)microseconds() ^ (iteration_counter++);

	/* here's the heart of the Win32 implementation of RNG_FillBuffer */
	if (CryptAcquireContext(&hCryptProv, 0, 0, PROV_DSS, CRYPT_VERIFYCONTEXT))
	{
		CryptGenRandom(hCryptProv, size, buffer);
		CryptReleaseContext(hCryptProv, 0);
	}

	MUTEX_UNLOCK();

	return size;
#else
	int ret, fd;

	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		return 0;
	ret = read(fd, buffer, size);
	close(fd);

	if (ret < 0)
		return 0;
	else
		return ret;
#endif
}


/*** Mersenne Twister ***/

#define VECTOR_SPACE 624     /* Length of state vector, do not change */

local u32 mt_state[VECTOR_SPACE];  /* Internal state vector */
local u32 mt_output[VECTOR_SPACE]; /* Destination for the hashed version */
local u32 mt_offset;               /* Offset to the next unused 32-bit block */

#define HIBIT(u)        ((u) & 0x80000000)      /* Mask all but highest-bit of u */
#define LOBIT(u)        ((u) & 0x00000001)      /* Mask all but lowest-bit of u */
#define LOBITS(u)       ((u) & 0x7fffffff)      /* Mask the highest-bit of u */
#define MIXBITS(u, v)   (HIBIT(u) | LOBITS(v))  /* Mix bits of two registers */

#define PERIOD_PARAM 397
local const u32 MAGIC_0[2] = {0, 0x9908b0df};

local void mt_initialize()
{
	RNG_FillBuffer((u8*)mt_state, sizeof(mt_state));
	mt_offset = VECTOR_SPACE;
}

local void mt_cleanup()
{
	memset(mt_state, 0, sizeof(mt_state));
	mt_offset = 0;
}

local void mt_iterate()
{
	u32 ii, jj;

	/* MT algorithm */
	for (ii = 0; ii < VECTOR_SPACE - PERIOD_PARAM; ++ii)
	{
		jj = MIXBITS(mt_state[ii], mt_state[ii+1]);
		mt_state[ii] = mt_state[ii + PERIOD_PARAM] ^ (jj >> 1) ^ MAGIC_0[LOBIT(jj)];
	}

	for (; ii < VECTOR_SPACE; ++ii)
	{
		jj = MIXBITS(mt_state[ii], mt_state[ii+1]);
		mt_state[ii] = mt_state[ii + PERIOD_PARAM - VECTOR_SPACE] ^ (jj >> 1) ^ MAGIC_0[LOBIT(jj)];
	}

	/* secure hash the result */
	for (ii = 0; ii < sizeof(mt_state); ii += 16)
	{
		struct MD5Context ctx;
		MD5Init(&ctx);
		MD5Update(&ctx, ((unsigned char const *)mt_state) + ii, 16);
		MD5Final(((unsigned char *)mt_output) + ii, &ctx);
	}

	/*
	 * There are three problems to be solved when using MT:
	 *     tempering - each 32-bit output block does not spread the wealth to all bits
	 *     internal state obfuscation - attackers shouldn't be able to guess this
	 *     seeding - knowing the seed is the same as knowing the internal state
	 *
	 * We do not want someone to be able to reconstruct the internal state vector,
	 * because that would allow them to predict the results of future calls.
	 *
	 * RNG is used to seed the generator because this satisfies the requirement that
	 * the internal state is unguessable, at least initially.
	 *
	 * The MT algorithm itself was written to have an extremely long period with good
	 * statistical properties.  MT designers realized that bits are not distributed
	 * very well across each 32-bit block of the state vector.  They use a tempering
	 * function like a miniature hash to make each bit as random as the others.
	 *
	 * 32-bit hash blocks are not secure, because they only offer 16-bit security.
	 * To put it another way, there may be some blocks which are weaker than others
	 * in statistical randomality.
	 *
	 * We apply SHA-256 to solve the tempering and internal state obfuscation problems
	 * by making the output a one-way function of the internal state.  Attackers cannot
	 * use the output of the generator to guess the internal state of the algorithm.
	 * Furthermore, each bit of input to SHA-256 affects one half the bits of output.
	 * This is much better as a tempering function also because it combines bits from
	 * 8 consecutive 32-bit blocks making the strength of each block more uniform.
	 *
	 * I see two problems with this approach yet to be addressed:
	 *     decrease in throughput
	 *         we really need some strong hash algo because it's an online game (cheaters)
	 *     the SHA may destroy some of those nice statistical properties of MT
	 *
	 * This function is called whenever the mt_state buffer has been exhausted for
	 * bits.  So: <RNG output> <MT-19937> [<SHA2> <use bits>] <MT> [<SHA> <use>] etc */

	mt_offset = 0;
}

local void mt_fill(u8 *buffer, int size)
{
	/* we give out bits in blocks of 32 */
	int needed = (size + 3) / 4;
	int remaining = VECTOR_SPACE - mt_offset;

	/* make sure there's at least one 32-bit block remaining in mt_output before the loop */
	if (!remaining)
		mt_iterate();

	/* satisfy requests that exceed the number of blocks available */
	while (remaining < needed)
	{
		/* we don't give out partial blocks here */
		memcpy(buffer, mt_output + mt_offset, remaining*4);

		mt_iterate();

		/* set up for next round of logic */
		buffer += remaining * 4;
		size -= remaining * 4;
		needed -= remaining;
		remaining = VECTOR_SPACE;
	}

	/* copy any whole blocks we can, ignoring the last one which may be partial */
	if (needed > 1)
	{
		--needed;
		memcpy(buffer, mt_output + mt_offset, needed*4);
		buffer += needed * 4;
		size -= needed * 4;
		mt_offset += needed;
	}

	/* copy the last block */
	if (size > 0)
	{
		u32 output = mt_output[mt_offset++];

		switch (size)
		{
			case 4: *(u32*)buffer = output; break;
			case 3: *buffer++ = (u8)(output & 0xff); output >>= 8;
			case 2: *buffer++ = (u8)(output & 0xff); output >>= 8;
			case 1: *buffer = (u8)(output & 0xff);
		}
	}
}

local u32 mt_next()
{
	if (mt_offset == VECTOR_SPACE)
		mt_iterate();

	return mt_output[mt_offset++];
}


/*** MCRNG -- Monte Carlo R.N.G. ***/

local int MCRNG_FillBuffer(void *buffer, int size)
{
	if (!buffer || size <= 0) return 0;
	MUTEX_LOCK();
	mt_fill(buffer, size);
	MUTEX_UNLOCK();
	return size;
}

local int MCRNG_Number(int start, int end)
{
	u32 range = (u32)(end - start + 1);
	u32 n, mask = 0xffffffff, ctr = 0x80000000;

	if (end < start)
		return start-1;
	else if (end == start)
		return start;

	/* produce a random number in the requested range without bias */
	--range;
	do {
		if (range & ctr)
			break;

		mask ^= ctr;
	} while (ctr >>= 1);
	++range;

	MUTEX_LOCK();

	do n = mt_next() & mask; while (n >= range);

	MUTEX_UNLOCK();

	return n + start;
}

local u32 MCRNG_Get32(void)
{
	u32 n;
	MUTEX_LOCK();
	n = mt_next();
	MUTEX_UNLOCK();
	return n;
}

local int MCRNG_rand(void)
{
	int n;
#if INT_MAX == RAND_MAX
	/* this is a really easy case, just use 31 bits of it */
	MUTEX_LOCK();
	n = (int)(mt_next() & 0x7fffffff);
	MUTEX_UNLOCK();
#else
	n = MCRNG_Number(0, RAND_MAX);
#endif
	return n;
}

local double Uniform(void)
{
	return (double)MCRNG_rand() / (double)(RAND_MAX+1.0);
}


/*** Iprng ***/

local Iprng prngint =
{
	INTERFACE_HEAD_INIT(I_PRNG, "prng")

	/* RNG offerings */
	RNG_FillBuffer,

	/* MCRNG offerings */
	MCRNG_FillBuffer,
	MCRNG_Number,
	MCRNG_Get32,
	MCRNG_rand,
	Uniform
};


/*** Module action pump ***/

EXPORT const char info_prng[] = CORE_MOD_INFO("prng");

EXPORT int MM_prng(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		iteration_counter = 0;

		mt_initialize();

		mm->RegInterface(&prngint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&prngint, ALLARENAS))
			return MM_FAIL;

		iteration_counter = 0;

		mt_cleanup();

		return MM_OK;
	}

	return MM_FAIL;
}
