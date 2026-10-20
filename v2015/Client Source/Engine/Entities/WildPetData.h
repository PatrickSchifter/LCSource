// ----------------------------------------------------------------------------
//  File : WILDPETDATA.h
//  Desc : Created by Dongmin
// ----------------------------------------------------------------------------
#ifndef	WILDPETDATA_H_
#define	WILDPETDATA_H_
#ifdef	PRAGMA_ONCE
	#pragma once
#endif

#include <Engine/Entities/Entity.h>
#include <vector>
#include <Engine/Help/LoadLod.h>
#include <Common/header/def_apet.h>

#define WILDPET_FLAG_ATTACK (1 << 0)	// °ø°Ý °¡´ÉÇÑ Æê À¯¹«
#define WILDPET_FLAG_EXP	(1 << 1)	// °æÇèÄ¡ ÃàÃ´ Æê À¯¹«

enum eWildPetAni
{
	WILD_PET_ANIM_START = 0, 
	WILD_PET_ANIM_IDLE1 = WILD_PET_ANIM_START,
	WILD_PET_ANIM_IDLE2,
	WILD_PET_ANIM_ATTACK1,
	WILD_PET_ANIM_ATTACK2,
	WILD_PET_ANIM_DAMAGE,
	WILD_PET_ANIM_DIE,
	WILD_PET_ANIM_WALK,
	WILD_PET_ANIM_RUN,
	WILD_PET_ANIM_HUNGRY,
	WILD_PET_ANIM_END = WILD_PET_ANIM_HUNGRY,
	WILD_PET_ANIM_TOTAL,
};

class ENGINE_API CWildPetData : public stApet, public LodLoader<CWildPetData>
{
public:
	static bool	loadEx(const char* FileName);
};

#endif