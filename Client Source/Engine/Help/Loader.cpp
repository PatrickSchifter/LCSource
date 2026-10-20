
#include "StdH.h"
#include "Loader.h"


bool TableLoader::load_file( const char* str_path, int strCnt )
{
	int		i, nStr;
	int		nIdx, nRow, nMax;
	FILE*	fp = NULL;
	char	buff[8196];
	int		nLegnth;

	fp = fopen(str_path, "rb");

	if (fp == NULL)
		return false;

	fread(&nRow, sizeof(int), 1, fp);
	fread(&nMax, sizeof(int), 1, fp);

	for (i = 0; i < nRow; ++i)
	{
		fread(&nIdx, sizeof(int), 1, fp);

		stSTRING	st;

		for (nStr = 0; nStr < strCnt; ++nStr)
		{
			fread(&nLegnth, sizeof(int), 1, fp);

			if (nLegnth > 0)
			{
				fread(buff, nLegnth, 1, fp);
				buff[nLegnth] = '\0';

				std::string str = buff;

				st._vecString.push_back(str);
			}
		}

		if (st._vecString.size() > 0)
		{
			_map.insert(std::make_pair(nIdx, st));
		}
	}

	fclose(fp);
	return true;
}

stSTRING& TableLoader::get_data( int index )
{
	map_iter iter = _map.find(index);

	if (iter != _map.end())
	{
		return iter->second;
	}

	// Was "return stSTRING();": a reference to a destroyed temporary (warning C4172 was
	// disabled above). It only worked by accident with VS2010.
	static stSTRING s_empty;
	return s_empty;
}

void TableLoader::release()
{
	_map.clear();
}
