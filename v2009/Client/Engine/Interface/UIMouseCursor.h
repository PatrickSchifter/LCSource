// ----------------------------------------------------------------------------
//  File : UIMouseCursor.h
//  Desc : Created by yjpark
// ----------------------------------------------------------------------------

#ifndef	UIMOUSECURSOR_H_
#define	UIMOUSECURSOR_H_
#ifdef	PRAGMA_ONCE
	#pragma once
#endif

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
#ifdef LC_PLATFORM_SDL
	void	SetCursorNULL(void) { PLT_SetCursor(NULL); }
#else
	void	SetCursorNULL(void) { SetCursor(LoadCursor(NULL, IDC_ARROW)); }
#endif
};


#endif	// UIMOUSECURSOR_H_

