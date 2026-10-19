#include <stdlib.h>
#include "LCCrypt.h"
#include "LCCrypt_Internel.h"

void CNM_InitKeyValue(CNM_KEY_TYPE* pKey)
{
	*pKey = CNM_INIT_KEY;
}

// The published 2009 client never changes keys: its CNM_NextKey calls are commented out
// ("edit by kevin", Engine/Network/MessageDispatcher.cpp) and CNM_MakeKeyFromSeed is empty, so
// both directions always use CNM_INIT_KEY. Rolling the server key with rand() (the original body)
// makes every message after the first undecryptable on either side, so this is a no-op too.
void CNM_NextKey(CNM_KEY_TYPE* pKey)
{
	(void)pKey;
}

void CNM_CopyKey(CNM_KEY_TYPE* pDestKey, const CNM_KEY_TYPE* pSrcKey)
{
	*pDestKey = *pSrcKey;
}

int CNM_Crypt(const unsigned char* pSrc, int nLenSrc, CNM_KEY_TYPE* pKey, unsigned char* pDest, unsigned char* pTmpBuf)
{
	return LCCrypt_Crypt(pSrc, nLenSrc, *pKey, pDest);
}

int CNM_Decrypt(const unsigned char* pSrc, int nLenSrc, CNM_KEY_TYPE* pKey, unsigned char* pDest, unsigned char* pTmpBuf)
{
	return LCCrypt_Decrypt(pSrc, nLenSrc, *pKey, pDest, pTmpBuf);
}

unsigned int CNM_MakeSeedForClient(const char* strID, const char* strPW, unsigned long nTickCount)
{
	return 0;
}

unsigned int CNM_MakeSeedForServer(int nRandomValue, int nServerPulse)
{
	return 0;
}

void CNM_MakeKeyFromSeed(CNM_KEY_TYPE* pKey, unsigned int nSeed)
{
	return ;
}
