#ifndef SE_INCL_LODREAD_H
#define SE_INCL_LODREAD_H
#ifdef PRAGMA_ONCE
  #pragma once
#endif

#include <stdio.h>

// The .lod loaders' length-prefixed strings, bounded by the field they are read into: data made
// for later clients has longer strings (the BR item names are 94-125 bytes against char[50]).
// A string that fits is read whole (terminated when there is room, as the field then had zeros
// after it); a longer one is cut to the field less its terminator and the rest skipped. Returns
// what fread returned, as the loaders' LOADSTR did.
inline size_t LC_ReadLodString(FILE *fp, void *pDest, size_t slSize)
{
  int iLen = 0;
  if (fread(&iLen, sizeof(int), 1, fp) != 1) return 0;
  if (iLen < 0) iLen = 0;
  const size_t slKeep = (size_t)iLen <= slSize ? (size_t)iLen : slSize-1;
  const size_t slRead = fread(pDest, slKeep, 1, fp);
  if (slKeep < slSize) ((char*)pDest)[slKeep] = 0;
  if ((size_t)iLen > slKeep) fseek(fp, (long)((size_t)iLen-slKeep), SEEK_CUR);
  return slRead;
}

// The same for a non-string field read with a length in front (ItemData's job): no terminator.
inline size_t LC_ReadLodField(FILE *fp, void *pDest, size_t slSize)
{
  int iLen = 0;
  if (fread(&iLen, sizeof(int), 1, fp) != 1) return 0;
  if (iLen < 0) iLen = 0;
  const size_t slKeep = (size_t)iLen <= slSize ? (size_t)iLen : slSize;
  const size_t slRead = fread(pDest, slKeep, 1, fp);
  if ((size_t)iLen > slKeep) fseek(fp, (long)((size_t)iLen-slKeep), SEEK_CUR);
  return slRead;
}

#endif  /* include-once check. */
