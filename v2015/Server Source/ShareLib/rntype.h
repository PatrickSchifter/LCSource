#ifndef __RENEW_TYPE_H__
#define __RENEW_TYPE_H__

#if defined(_WIN32) || defined(WIN32)
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#include <stdio.h>
#include <limits.h>
#include <stdint.h>
#include <time.h>
#include <map>
#include <string.h>

// Fixed-width types: the original server was a 32-bit Linux build (ILP32), where `long` was
// 32 bits. On LP64 (macOS/Linux x86_64/arm64) `long` is 64 bits, so tLONG/tULONG are pinned
// to 32 bits to keep the sizes the original code was written for.
typedef char					tCHAR;
typedef unsigned char			tUCHAR;
typedef unsigned char			tBYTE;
typedef int16_t					tSINT;
typedef uint16_t				tUSINT;
typedef int32_t					tINT;
typedef uint32_t				tUINT;
typedef int32_t					tLONG;
typedef uint32_t				tULONG;
typedef float					tFLOAT;
typedef double					tDOUBLE;
typedef int64_t					tINT64;
typedef	uint64_t				tUINT64;

#ifndef WIN32
// tTIME and tSIZE stay native (64-bit on LP64). They are not used in any packet or file
// structure; do not put them in one (use tINT/tINT64 there).
typedef time_t					tTIME;
typedef size_t					tSIZE;
//typedef bool					BOOL;
typedef uint32_t				DWORD;
typedef unsigned char			BYTE;
typedef DWORD					RET_VAL;
#else
#define snprintf _snprintf
#endif

#if defined(__cplusplus) && __cplusplus >= 201103L
static_assert(sizeof(tSINT) == 2 && sizeof(tUSINT) == 2, "tSINT must be 16-bit");
static_assert(sizeof(tINT) == 4 && sizeof(tUINT) == 4, "tINT must be 32-bit");
static_assert(sizeof(tLONG) == 4 && sizeof(tULONG) == 4, "tLONG must be 32-bit (as in the original ILP32 build)");
static_assert(sizeof(tINT64) == 8 && sizeof(tUINT64) == 8, "tINT64 must be 64-bit");
#ifndef WIN32
static_assert(sizeof(DWORD) == 4, "DWORD must be 32-bit");
#endif
#endif

inline tINT utf8_charbytes( tINT c )
{
	if( (c & 0x80) == 0 )
		return 1;

	if( (c & 0xe0) == 0xc0 )
		return 2;

	if( (c & 0xf0) == 0xe0 )
		return 3;

	if( (c & 0xf8) == 0xf0 )
		return 4;

	return -1;
}

inline tINT utf8_strlen( const tCHAR* utf8_str )
{
	tINT byte_len = (tINT)strlen( utf8_str );
	tINT char_len = 0;

	while( byte_len > 0 )
	{
		tINT len = utf8_charbytes( *utf8_str );
		if( len == -1 )
			return -1;

		utf8_str += len;
		byte_len -= len;

		char_len++;
	}

	return char_len;
}

#endif
