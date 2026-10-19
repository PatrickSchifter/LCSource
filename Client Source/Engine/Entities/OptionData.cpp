#include "StdH.h"

#include <Engine/Templates/StaticArray.cpp>
#include <Engine/Entities/OptionData.h>
#include <Engine/Interface/UIManager.h>
#include <Engine/Help/LoadLod.h>

//-----------------------------------------------------------------------------
// Purpose: 아이템 데이터를 파일로부터 읽어들입니다.
// Input  : &apOptionData - 아이템 목록이 저장될 배열.
//			FileName - 파일명.
// Output : 	static int
//-----------------------------------------------------------------------------
bool COptionData::loadEx(const char* FileName)
{	
	FILE*	fp = NULL;

	fp = fopen(FileName, "rb");

	if (fp == NULL)
		return false;

	fread(&_nSize, sizeof(int), 1, fp);

	if (_nSize <= 0)
	{
		fclose(fp);
		return false;
	}

	stOption* pdata = new stOption[_nSize];
	fread(pdata, sizeof(stOption) * _nSize, 1, fp);
	fclose(fp);

	for (int i = 0; i < _nSize; i++)
	{
		COptionData* ptmp = new COptionData;
		memcpy(ptmp, &pdata[i], sizeof(stOption));
		if (_mapdata.insert(std::make_pair(ptmp->type, ptmp)).second == false)
		{
			delete ptmp;
			ptmp = NULL;
		}
	}

	m_dummy = new COptionData; // 더미데이타 생성
	memset(m_dummy, 0, sizeof(stOption));

	if (pdata != NULL)
	{
		delete[] pdata;
		pdata = NULL;
	}

	return true;
}

// One length-prefixed string of the older titletool.lod; copied into pDest (cut to its size)
// when there is one.
static bool ReadOldTitleString(FILE* fp, char* pDest, int iDestSize)
{
	int iLen = 0;
	char acBuf[1024];

	if (fread(&iLen, sizeof(int), 1, fp) != 1 || iLen < 0 || iLen > (int)sizeof(acBuf))
		return false;

	if (iLen > 0 && fread(acBuf, iLen, 1, fp) != 1)
		return false;

	if (pDest != NULL)
	{
		int iCopy = iLen < iDestSize ? iLen : iDestSize - 1;
		memcpy(pDest, acBuf, iCopy);
		pDest[iCopy] = '\0';
	}

	return true;
}

// The older titletool.lod (the BR 2014 data has it): records of variable size until the end of
// the file, and the count in front is the highest index. Each one is the index, the name and the
// description (not kept: the newer layout has neither, the name shown is the title item's), the
// three effect files, the colors, five option index/level pairs and the title item.
static bool LoadOldTitles(FILE* fp)
{
	for (;;)
	{
		stTitle st;
		memset(&st, 0, sizeof(st));

		if (fread(&st.index, sizeof(int), 1, fp) != 1)
			return true;

		bool bOk = ReadOldTitleString(fp, NULL, 0) && ReadOldTitleString(fp, NULL, 0)
			&& ReadOldTitleString(fp, st.a_effect, sizeof(st.a_effect))
			&& ReadOldTitleString(fp, st.a_attack, sizeof(st.a_attack))
			&& ReadOldTitleString(fp, st.a_damage, sizeof(st.a_damage))
			&& fread(&st.a_icolor, sizeof(st.a_icolor), 1, fp) == 1
			&& fread(&st.a_ibgcolor, sizeof(st.a_ibgcolor), 1, fp) == 1;

		for (int i = 0; bOk && i < DEF_NICK_OPTION_MAX; i++)
		{
			bOk = fread(&st.a_option_index[i], sizeof(int), 1, fp) == 1
				&& fread(&st.a_option_level[i], sizeof(BYTE), 1, fp) == 1;
		}

		if (!bOk || fread(&st.a_item_index, sizeof(int), 1, fp) != 1)
			return false;

		st.a_enable = 1;

		CNickNameData* ptmp = new CNickNameData;
		memcpy(ptmp, &st, sizeof(stTitle));
		if (CNickNameData::_mapdata.insert(std::make_pair(ptmp->getindex(), ptmp)).second == false)
			delete ptmp;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 호칭 데이터를 파일로부터 읽어들입니다.
// Input  : &aNickNameData - 호칭 목록이 저장될 배열.
//			FileName - 파일명.
// Output : 	static int (호칭 총 갯수)
//-----------------------------------------------------------------------------
bool CNickNameData::loadEx(const char* FileName)
{
	FILE*	fp = NULL;

	fp = fopen(FileName, "rb");

	if (fp == NULL)
		return false;

	fread(&_nSize, sizeof(int), 1, fp);

	if (_nSize <= 0)
	{
		fclose(fp);
		return false;
	}

	long lData = ftell(fp);
	fseek(fp, 0, SEEK_END);
	long lFileSize = ftell(fp);
	fseek(fp, lData, SEEK_SET);

	if (lFileSize != lData + (long)(sizeof(stTitle) * _nSize))
	{
		bool bLoaded = LoadOldTitles(fp);
		fclose(fp);

		m_dummy = new CNickNameData;
		memset(m_dummy, 0, sizeof(stTitle));
		return bLoaded;
	}

	stTitle* pdata = new stTitle[_nSize];
	fread(pdata, sizeof(stTitle) * _nSize, 1, fp);
	fclose(fp);

	for (int i = 0; i < _nSize; i++)
	{
		CNickNameData* ptmp = new CNickNameData;
		memcpy(ptmp, &pdata[i], sizeof(stTitle));
		if (_mapdata.insert(std::make_pair(ptmp->getindex(), ptmp)).second == false)
		{
			delete ptmp;
			ptmp = NULL;
		}
	}

	m_dummy = new CNickNameData; // 더미데이타 생성
	memset(m_dummy, 0, sizeof(stTitle));
	
	if (pdata != NULL)
	{
		delete[] pdata;
		pdata = NULL;
	}

	return true;
}