// ----------------------------------------------------------------------------
//  File : UIMouseCursor.h
//  Desc : Created by yjpark
// ----------------------------------------------------------------------------

#ifndef	UIMOUSECURSOR_H_
#define	UIMOUSECURSOR_H_
#ifdef	PRAGMA_ONCE
	#pragma once
#endif


// UI mouse cursor type
enum UIMCType
{
	UMCT_NORMAL			= 0,
	UMCT_ATT_NORMAL		= 1,
	UMCT_ATT_SKILL		= 2,
	UMCT_ATT_MAGIC		= 3,
	UMCT_ATT_BOW		= 4,
	UMCT_PICK			= 5,
	UMCT_TALK			= 6,
	UMCT_PRODUCE		= 7,
	UMCT_SIZE			= 8,
	UMCT_ZOOMIN			= 9,
	UMCT_SIGNAL			= 10,
	UMCT_TOTAL			= 11,
	UMCT_CURRENT		= -1,
};


#include <Engine/Platform/Platform.h>

// ----------------------------------------------------------------------------
// Name : CUIMouseCursor
// Desc :
// ----------------------------------------------------------------------------
class CUIMouseCursor
{
protected:
	UIMCType		m_umctType;						// Type of current mouse cursor
	HCURSOR			m_hCursors[UMCT_TOTAL];			// Handles of mouse cursor

public:
	CUIMouseCursor() { m_umctType = UMCT_NORMAL; memset(m_hCursors, 0, sizeof(m_hCursors)); }
	~CUIMouseCursor() {}

	// Type
	void	SetCursorType( UIMCType umctType = UMCT_CURRENT ) 
	{ 
		if( umctType != UMCT_CURRENT )
			m_umctType = umctType;

#ifdef LC_PLATFORM_SDL
		PLT_SetCursor(m_hCursors[m_umctType]);
#else
		SetCursor(m_hCursors[m_umctType]);
#endif
	}
	UIMCType	GetCursorType() const { return m_umctType; }

	// Handles
	void	SetCursorHandle( UIMCType umctType, HCURSOR hCursor ) { m_hCursors[umctType] = hCursor; }
};


#endif	// UIMOUSECURSOR_H_

