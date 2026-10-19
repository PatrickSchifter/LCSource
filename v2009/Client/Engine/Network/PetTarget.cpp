#include "StdH.h"

#include <Engine/Base/Console.h>
#include <Engine/Network/PetTarget.h>
#include <Engine/Templates/StaticArray.cpp>
#include <Engine/Network/CNetwork.h>
#include <Engine/Effect/CEffectGroupManager.h>
#include <Engine/Entities/EntityClass.h>
#include <Engine/Entities/EntityProperties.h>
#include <Engine/Ska/StringTable.h>

CPetTargetInfom::CPetTargetInfom(void)
{	
	Init();
}

CPetTargetInfom::~CPetTargetInfom(void)
{

}

void CPetTargetInfom::Init(void)
{
	lIndex			= -1;
	fMaxHealth		= 0.0f;
	fHealth			= 0.0f;
	fMaxHungry		= 0.0f;
	fHungry			= 0.0f;
	bIsActive		= FALSE;
	iLevel			= 0;	
	pen_pEntity		= NULL;
	iType			= -1;
	iAge			= -1;
	lAbility		= -1;
	TransformIndex	= 0;
	strNameCard		= CTString("");
}

/*
 *  Constructor.
 */
CPetTarget::CPetTarget(void) 
{
	pet_Index			= -1;
	pet_iType			= -1;                
	pet_iAge			= -1;                
	pet_iClientIndex	= -1;
	pet_yLayer			= 0;
	pet_pEntity			= NULL;
	pet_pNormalEffect	= NULL;
	pet_sbAttributePos	= ATTC_UNWALKABLE;
	pet_OwnerIndex		= -1;
	pet_OwnerName		= CTString("");
	pet_Name			= CTString("");
	pet_strNameCard		= CTString("");
}

/*
 *  Destructor.
 */
CPetTarget::~CPetTarget(void) 
{
	if(pet_pNormalEffect)
	{
		DestroyEffectGroupIfValid(pet_pNormalEffect);
		pet_pNormalEffect = NULL;
	}
}

CPetTarget::CPetTarget(const CPetTarget &other)
{
	pet_Index	= other.pet_Index;
	pet_iType	= other.pet_iType;
	pet_iAge	= other.pet_iAge;
	pet_iClientIndex = other.pet_iClientIndex;	
	pet_pEntity = other.pet_pEntity;
	pet_Name	= other.pet_Name;
	pet_OwnerName	= other.pet_OwnerName;
	pet_yLayer = other.pet_yLayer;
	//Hardcoding, status effect¿¡ vtableÀÌ Ãß°¡µÇ°Å³ª »ó¼ÓÀÌ µÇ´Â °æ¿ì ¹®Á¦ »ý±è.
	memcpy(&pet_statusEffect, &other.pet_statusEffect, sizeof(pet_statusEffect));
	memset((void*)&other.pet_statusEffect, 0, sizeof(other.pet_statusEffect));
	pet_pNormalEffect = other.pet_pNormalEffect;
	pet_sbAttributePos	= other.pet_sbAttributePos;
	pet_OwnerIndex = other.pet_OwnerIndex;
}

CPetTarget &CPetTarget::operator=(const CPetTarget &other)
{
	pet_Index = other.pet_Index;
	pet_iType = other.pet_iType;
	pet_iAge	= other.pet_iAge;
	pet_iClientIndex = other.pet_iClientIndex;	
	pet_pEntity = other.pet_pEntity;
	pet_Name = other.pet_Name;
	pet_OwnerName = other.pet_OwnerName;
	pet_yLayer = other.pet_yLayer;
	//Hardcoding, status effect¿¡ vtableÀÌ Ãß°¡µÇ°¡³ª »ó¼ÓÀÌ µÇ´Â °æ¿ì ¹®Á¦ »ý±è.
	memcpy(&pet_statusEffect, &other.pet_statusEffect, sizeof(pet_statusEffect));
	memset((void*)&other.pet_statusEffect, 0, sizeof(other.pet_statusEffect));
	pet_pNormalEffect = other.pet_pNormalEffect;
	pet_sbAttributePos	= other.pet_sbAttributePos;
	pet_OwnerIndex = other.pet_OwnerIndex;
	pet_strNameCard =other.pet_strNameCard;
	return *this;
}


void CPetTarget::Init()
{
	CEntity	*penEntity;
	if( _pNetwork->ga_World.EntityExists( pet_iClientIndex, penEntity ) ) 
	{
		penEntity->en_pPetTarget = NULL;
		if( penEntity == _pNetwork->_TargetInfoReal.pen_pEntity )
		{
			_pNetwork->_TargetInfoReal.Init();
		}

		if( penEntity == _pNetwork->_TargetInfo.pen_pEntity )
		{
			_pNetwork->_TargetInfo.Init();
		}

		penEntity->Destroy( FALSE );
	}

	pet_Index			= -1;
	pet_iType			= -1;                
	pet_iAge			= -1;
	pet_iClientIndex	= -1;
	pet_yLayer			= 0;
	pet_pEntity			= NULL;
	pet_statusEffect.Reset();
	pet_sbAttributePos	= ATTC_UNWALKABLE;
	pet_OwnerIndex		= -1;
	pet_OwnerName		= CTString("");
	pet_Name			= CTString("");
	pet_strNameCard		= CTString("");
	if(pet_pNormalEffect)
	{
		DestroyEffectGroupIfValid(pet_pNormalEffect);
		pet_pNormalEffect = NULL;
	}
}
//°­µ¿¹Î ¼öÁ¤ ³¡ ½Ì±Û ´øÁ¯ ÀÛ¾÷		07.27
#include <Engine/Network/Server.h>	// TEST
void CPetTarget::SetData( INDEX index, CTString& strName, CTString& strOwnerName, INDEX iOwnerIndex, INDEX iPetType, INDEX iPetAge, CEntity *pEntity, SBYTE sbyLayer, CTString strNameCard)
{
	pet_Index		= index;
	pet_iType		= iPetType;    
	pet_iAge		= iPetAge;
	pet_Name		= strName;	
	pet_OwnerName	= strOwnerName;	
	pet_pEntity		= pEntity;
	pet_yLayer		= sbyLayer;
	pet_OwnerIndex	= iOwnerIndex;
	pet_strNameCard = strNameCard;
}

void CPetTarget::SetClientPetId(INDEX index)
{	
	pet_iClientIndex = index;	
}

CWildPetInfo::CWildPetInfo()
{
	bIsActive = FALSE;
	m_nLevel = -1;
	m_nType	= -1;
	m_strName = "";
	m_nStr	= -1;
	m_nCon	= -1;
	m_nDex	= -1;
	m_nInt	= -1;
	m_nHP	= -1;
	m_nMaxHP	= -1;
	m_nMP	= -1;
	m_nMaxMP	= -1;
	m_nFaith	= -1;
	m_nStm		= -1;
	m_nMaxFaith	= -1;
	m_nMaxStm	= -1;
	m_nSpeed	= -1;
	m_nAISlot	= -1;
	m_nNetIndex = -1;
	pet_iClientIndex = -1;
	pet_pEntity = NULL;
	int i = 0; for( ; i < STATE_END; i++)
	{m_nWildPetState[i] = -1;}

	for(i = 0; i < WILDPET_WEAR_TOTAL; i++)
	{
		m_nPetWearIndex[i] = -1;
		m_nPetWearPlus[i] =  -1;
	}
}
CWildPetInfo::~CWildPetInfo()
{

}


CWildPetInfo::CWildPetInfo(const CWildPetInfo &other)
{
	(*this) = other;
}

CWildPetInfo &CWildPetInfo::operator=(const CWildPetInfo &other)
{
	bIsActive = other.bIsActive;
	m_nIndex = other.m_nIndex; // Æê Åø ÀÎµ¦½º
	m_nNetIndex = other.m_nNetIndex;
	m_nOwnerIndex = other.m_nOwnerIndex;
	m_nLevel = other.m_nLevel; // °ø°Ý Æê ·¹º§
	m_nLevelupPoint = other.m_nLevelupPoint; // °ø°Ý Æê ¼ºÀåÆ÷ÀÎÆ®
	m_nType = other.m_nType; // °ø°Ý Æê Å¸ÀÔ
	m_strName = other.m_strName; // °ø°Ý Æê ÀÌ¸§
	// Bagic
	m_nStr = other.m_nStr; // °ø°Ý Æê Èû
	m_nStrPlus = other.m_nStrPlus; 	//	°ø°Ý Æê Èû Ãß°¡
	m_nCon = other.m_nCon; // °ø°Ý Æê Ã¼·Â
	m_nConPlus = other.m_nConPlus;		// °ø°Ý Æê Ã¼·Â Ãß°¡	
	m_nDex = other.m_nDex;			// °ø°Ý Æê ¹ÎÃ¸
	m_nDexPlus = other.m_nDexPlus;		// °ø°Ý Æê ¹ÎÃ¸ Ãß°¡
	m_nInt = other.m_nInt;			// °ø·Â Æê ÁöÇý
	m_nIntPlus = other.m_nIntPlus;		// °ø°Ý Æê ÁöÇý Ãß°¡
	m_exp = other.m_exp;			// °ø°Ý Æê °æÇèÄ¡
	m_next_exp = other.m_next_exp;		// °ø°Ý Æê ¸Æ½º(·¹º§¾÷) °æÇèÄ¡
	m_nHP = other.m_nHP;			// °ø°Ý Æê HP
	m_nMP = other.m_nMP;			// °ø°Ý Æê MP
	m_nMaxHP = other.m_nMaxHP;		// °ø°Ý Æê Max HP
	m_nMaxMP = other.m_nMaxMP;		// °ø°Ý Æê Max MP
	m_nFaith = other.m_nFaith;		// °ø°Ý Æê Ãæ¼º½É
	m_nStm = other.m_nStm;			// °ø°Ý Æê ¹è°íÇÄ
	m_nMaxFaith = other.m_nMaxFaith;	// °ø°Ý Æê ÃÖ´ë Ãæ¼º½É
	m_nMaxStm = other.m_nMaxStm;		// °ø°Ý Æê ÃÖ´ë ¹è°íÇÄ
	m_nSpeed = other.m_nSpeed;		// °ø°Ý Æê ÀÌµ¿ ¼Óµµ
	m_nAISlot = other.m_nAISlot;		// °ø°Ý Æê AI ½½·Ô°³¼ö
	pet_iClientIndex = other.pet_iClientIndex; //°ø°Ý Æê Client World Index
	pet_pEntity = other.pet_pEntity;
	m_sbYlayer = other.m_sbYlayer;
	m_sbAttributePos = other.m_sbAttributePos;
	// state
	INDEX i;

	for (i=0; i<STATE_END; ++i)
	{
		m_nWildPetState[i] = other.m_nWildPetState[i];
		m_nWildPetStatePlus[i] = other.m_nWildPetStatePlus[i];
	}

	for (i=0; i<WILDPET_WEAR_TOTAL; ++i)
	{
		m_nPetWearIndex[i] = other.m_nPetWearIndex[i];
		m_nPetWearPlus[i] = other.m_nPetWearPlus[i];
	}

	return (*this);
}

void CWildPetInfo::Init()
{
	CEntity	*penEntity;
	if( _pNetwork->ga_World.EntityExists( pet_iClientIndex, penEntity ) ) 
	{
		penEntity->en_pWildPetInfo = NULL;
		if( penEntity == _pNetwork->_TargetInfoReal.pen_pEntity )
		{
			_pNetwork->_TargetInfoReal.Init();
		}

		if( penEntity == _pNetwork->_TargetInfo.pen_pEntity )
		{
			_pNetwork->_TargetInfo.Init();
		}

		penEntity->Destroy( FALSE );		
	}
	bIsActive = FALSE;
	m_nIndex = -1;
	m_nLevel = -1;
	m_nType	= -1;
	m_strName = "";
	m_nStr	= -1;
	m_nCon	= -1;
	m_nDex	= -1;
	m_nInt	= -1;
	m_nHP	= -1;
	m_nMaxHP	= -1;
	m_nMP	= -1;
	m_nMaxMP	= -1;
	m_nFaith	= -1;
	m_nStm		= -1;
	m_nMaxFaith	= -1;
	m_nMaxStm	= -1;
	m_nSpeed	= -1;
	m_nAISlot	= -1;
	m_nNetIndex = -1;
	pet_iClientIndex = -1;
	pet_pEntity = NULL;
	
	int i = 0; for( ; i < STATE_END; i++)
	{m_nWildPetState[i] = -1;}

	for(i = 0; i < WILDPET_WEAR_TOTAL; i++)
	{
		m_nPetWearIndex[i] = -1;
		m_nPetWearPlus[i] =  -1;
	}

}

void CWildPetInfo::SetWildPetToEntity(CEntity *pEntity, INDEX nindex, INDEX *nWearIndex, INDEX *nWearPlus)
{
	CEntity* tmpOwner = NULL;

	for(int i = 0; i < WILDPET_WEAR_TOTAL; i++)
	{
		m_nPetWearIndex[i] = nWearIndex[i];
		m_nPetWearPlus[i] =  nWearPlus[i];
	}
	CWildPetData pt = _pNetwork->wo_aWildPetData[nindex];
	pet_pEntity = pEntity;
	
	CEntityProperty &eppropertyWalk	= *(pEntity->PropertyForTypeAndID(CEntityProperty::EPT_INDEX, 100));
	ENTITYPROPERTY(&*pEntity, eppropertyWalk.ep_slOffset, INDEX)	= ska_GetIDFromStringTable(pt.m_WildPetData.PetAni[WILD_PET_ANIM_WALK]);

	CEntityProperty &eppropertyIdle1 = *(pEntity->PropertyForTypeAndID(CEntityProperty::EPT_INDEX, 101));
	ENTITYPROPERTY(&*pEntity, eppropertyIdle1.ep_slOffset, INDEX)	= ska_GetIDFromStringTable(pt.m_WildPetData.PetAni[WILD_PET_ANIM_IDLE1]);

	CEntityProperty &eppropertyIdle2	= *(pEntity->PropertyForTypeAndID(CEntityProperty::EPT_INDEX, 102));
	ENTITYPROPERTY(&*pEntity, eppropertyIdle2.ep_slOffset, INDEX)	= ska_GetIDFromStringTable(pt.m_WildPetData.PetAni[WILD_PET_ANIM_IDLE2]);

	CEntityProperty &eppropertyRun	= *(pEntity->PropertyForTypeAndID(CEntityProperty::EPT_INDEX, 103));
	ENTITYPROPERTY(&*pEntity, eppropertyRun.ep_slOffset, INDEX)	= ska_GetIDFromStringTable(pt.m_WildPetData.PetAni[WILD_PET_ANIM_RUN]);

	CEntityProperty &eppropertyDeath	= *(pEntity->PropertyForTypeAndID(CEntityProperty::EPT_INDEX, 104));
	ENTITYPROPERTY(&*pEntity, eppropertyDeath.ep_slOffset, INDEX)	= ska_GetIDFromStringTable(pt.m_WildPetData.PetAni[WILD_PET_ANIM_DIE]);

	CEntityProperty &eppropertyDamage	= *(pEntity->PropertyForTypeAndID(CEntityProperty::EPT_INDEX, 105));
	ENTITYPROPERTY(&*pEntity, eppropertyDamage.ep_slOffset, INDEX)	= ska_GetIDFromStringTable(pt.m_WildPetData.PetAni[WILD_PET_ANIM_DAMAGE]);

	CEntityProperty &eppropertyAttack1	= *(pEntity->PropertyForTypeAndID(CEntityProperty::EPT_INDEX, 106));
	ENTITYPROPERTY(&*pEntity, eppropertyAttack1.ep_slOffset, INDEX)	= ska_GetIDFromStringTable(pt.m_WildPetData.PetAni[WILD_PET_ANIM_ATTACK1]);

	CEntityProperty &eppropertyAttack2	= *(pEntity->PropertyForTypeAndID(CEntityProperty::EPT_INDEX, 107));
	ENTITYPROPERTY(&*pEntity, eppropertyAttack2.ep_slOffset, INDEX)	= ska_GetIDFromStringTable(pt.m_WildPetData.PetAni[WILD_PET_ANIM_ATTACK2]);

	CEntityProperty &eppropertyLevelup	= *(pEntity->PropertyForTypeAndID(CEntityProperty::EPT_INDEX, 109)); // ¹è°íÇÄ
	ENTITYPROPERTY(&*pEntity, eppropertyLevelup.ep_slOffset, INDEX)	= ska_GetIDFromStringTable(pt.m_WildPetData.PetAni[WILD_PET_ANIM_HUNGRY]);	

	CEntityProperty &eppropertysbAttributePos	= *(pEntity->PropertyForTypeAndID(CEntityProperty::EPT_INDEX, 115));
	ENTITYPROPERTY(&*pEntity, eppropertysbAttributePos.ep_slOffset, INDEX)	= m_sbAttributePos;

	CDLLEntityClass *pdecDLLBaseClass	= pEntity->GetClass()->ec_pdecDLLClass->dec_pdecBase;
	CDLLEntityClass *pdecDLLBaseBaseClass	= pdecDLLBaseClass->dec_pdecBase;

	BOOL bAI = FALSE;
	if(m_nOwnerIndex == _pNetwork->MyCharacterInfo.index)
	{
		bAI = TRUE;
		tmpOwner = CEntity::GetPlayerEntity(0);
	}
	else
	{
		tmpOwner = _pNetwork->GetEntityByTypeIndex(0, m_nOwnerIndex);
	}

	CEntityProperty &epPropertyEntity	= *pdecDLLBaseClass->PropertyForTypeAndID(CEntityProperty::EPT_ENTITYPTR, 1); //8);	// Owner
	ENTITYPROPERTY( &*pEntity, epPropertyEntity.ep_slOffset, CEntityPointer)		= tmpOwner;

	CEntityProperty &epPropertyUseAI	= *pdecDLLBaseBaseClass->PropertyForTypeAndID(CEntityProperty::EPT_BOOL, 193);	// AI »ç¿ë À¯¹«.
	ENTITYPROPERTY( &*pEntity, epPropertyUseAI.ep_slOffset, INDEX)		= bAI;

	// ÀÌµ¿ ¼Óµµ...
	CEntityProperty &epPropertyWalkSpeed		= *pdecDLLBaseBaseClass->PropertyForTypeAndID(CEntityProperty::EPT_FLOAT, 10);	// NPC Walk Speed
	ENTITYPROPERTY( &*pEntity, epPropertyWalkSpeed.ep_slOffset, FLOAT)			= 3.5f;

	CEntityProperty &epPropertyAttackRunSpeed	= *pdecDLLBaseBaseClass->PropertyForTypeAndID(CEntityProperty::EPT_FLOAT, 12);	// NPC Attack Run Speed
	ENTITYPROPERTY( &*pEntity, epPropertyAttackRunSpeed.ep_slOffset, FLOAT)		= 7.0f;
	CEntityProperty &epPropertyCloseRunSpeed	= *pdecDLLBaseBaseClass->PropertyForTypeAndID(CEntityProperty::EPT_FLOAT, 14);	// NPC Close Run Speed
	ENTITYPROPERTY( &*pEntity, epPropertyCloseRunSpeed.ep_slOffset, FLOAT)		= 7.0f;

		// °ø°Ý °Å¸®...
	CEntityProperty &epPropertyAttackDistance	= *pdecDLLBaseBaseClass->PropertyForTypeAndID(CEntityProperty::EPT_FLOAT, 20);	// Attack Distance
	ENTITYPROPERTY( &*pEntity, epPropertyAttackDistance.ep_slOffset, FLOAT)		= 4.0f;
	
	CEntityProperty &epPropertyStopDistance		= *pdecDLLBaseBaseClass->PropertyForTypeAndID(CEntityProperty::EPT_FLOAT, 24);	// Attack Distance
	ENTITYPROPERTY( &*pEntity, epPropertyStopDistance.ep_slOffset, FLOAT)		= 3.0f;

	CEntityProperty &epPropertyCloseDistance	= *pdecDLLBaseBaseClass->PropertyForTypeAndID(CEntityProperty::EPT_FLOAT, 21);	// Attack Distance
	ENTITYPROPERTY( &*pEntity, epPropertyCloseDistance.ep_slOffset, FLOAT)		= 4.1f;
}

















