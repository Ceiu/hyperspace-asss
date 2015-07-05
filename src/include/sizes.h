
/* dist: public */

#ifndef __PACKETS_SIZES_H
#define __PACKETS_SIZES_H

#if (__STDC_VERSION__ >= 199901L)
/* try c99 if we can */
#include <stdint.h>
#elif defined(__FreeBSD__)
/* someone said this works on freebsd */
#include <sys/types.h>
#elif defined(__GNUC__)
/* this might work on recent gcc too */
#include <stdint.h>
#elif defined(_MSC_VER)
/* on windows try ms visual c */
typedef __int8 int8_t;
typedef __int16 int16_t;
typedef __int32 int32_t;
typedef __int64 int64_t;
typedef unsigned __int8 uint8_t;
typedef unsigned __int16 uint16_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;
#else
/* this might work on other 32 bit platforms */
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
#endif

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#endif

