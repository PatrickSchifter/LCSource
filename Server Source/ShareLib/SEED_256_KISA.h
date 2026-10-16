/*******************************************************************************
*
* FILE:         SEED_256_KISA.h
*
* DESCRIPTION:  header file for SEED_256_KISA.c
*
*******************************************************************************/

#ifndef _SEED_256_KISA_H
#define _SEED_256_KISA_H

/********************** Include files ************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/********************* Type Definitions **********************/

#ifndef TYPE_DEFINITION
#define TYPE_DEFINITION
// Fixed-width: the cipher needs DWORD to be exactly 32 bits (`unsigned long` is 64-bit on LP64).
#include <stdint.h>
typedef uint32_t            DWORD;
typedef uint16_t            WORD;
typedef unsigned char           BYTE;
#endif

/***************************** Endianness Define **************/
// If endianness is not defined correctly, you must modify here.
// SEED uses the Little endian as a defalut order

#if __alpha__   ||      __alpha ||      __i386__        ||      i386    ||      _M_I86  ||      _M_IX86 ||      \
        __OS2__         ||      sun386  ||      __TURBOC__      ||      vax             ||      vms             ||      VMS             ||      __VMS
#define SEED_LITTLE_ENDIAN
#else
#define SEED_BIG_ENDIAN
#endif

/******************* Constant Definitions *********************/

#define NoRounds         24
#define NoRoundKeys      (NoRounds*2)
#define SeedBlockSize    16    /* in bytes */
#define SeedBlockLen     128   /* in bits */

/********************** Common Macros ************************/

#if defined(_MSC_VER)
#define ROTL(x, n)     (_lrotl((x), (n)))
#define ROTR(x, n)     (_lrotr((x), (n)))
#else
#define ROTL(x, n)     (((x) << (n)) | ((x) >> (32-(n))))
#define ROTR(x, n)     (((x) >> (n)) | ((x) << (32-(n))))
#endif

/**************** Function Prototype Declarations **************/

void SeedEncrypt(		/* encryption function */
	BYTE *pbData, 				// [in,out]	data to be encrypted
	DWORD *pdwRoundKey			// [in]			round keys for encryption
);
void SeedDecrypt(		/* decryption function */
	BYTE *pbData, 				// [in,out]	data to be decrypted
	DWORD *pdwRoundKey			// [in]			round keys for decryption
);
void SeedRoundKey(		/* key scheduling function */
	DWORD *pdwRoundKey, 		// [out]		round keys for encryption or decryption
	BYTE *pbUserKey				// [in]			secret user key
);

/*************************** END OF FILE **************************************/
#endif
