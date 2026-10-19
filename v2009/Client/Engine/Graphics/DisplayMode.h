#ifndef SE_INCL_DISPLAYMODE_H
#define SE_INCL_DISPLAYMODE_H
#ifdef PRAGMA_ONCE
  #pragma once
#endif

#include <Engine/Base/Lists.h>
#include <Engine/Base/CTString.h>

// display mode bit-depth
enum DisplayDepth
{
	DISPD_NONE    = -1,
	DISPD_DEFAULT =  0,
	DISPD_16BIT   =  1,
	DISPD_32BIT   =  2,
	DISPD_24BIT   =  3, // for z-buffer
  // the names the 2009 game code uses
  DD_NONE    = DISPD_NONE,
  DD_DEFAULT = DISPD_DEFAULT,
  DD_16BIT   = DISPD_16BIT,
  DD_32BIT   = DISPD_32BIT,
  DD_24BIT   = DISPD_24BIT,
};

/*
 *  Structure that holds display mode description
 */

class ENGINE_API CDisplayMode {
public:
  PIX dm_pixSizeI; // size of screen in pixels
  PIX dm_pixSizeJ;
  enum DisplayDepth dm_ddDepth;  // bits per pixel for color

  /* Default constructor. */
  CDisplayMode(void);

  // get depth string
  CTString DepthString(void) const;
  // check if mode is dualhead, triplehead, widescreen or fullscreen
  BOOL IsDualHead(void);
  BOOL IsTripleHead(void);
  BOOL IsWideScreen(void);
};


#endif  /* include-once check. */

