#ifndef SE_INCL_DRAWPORT_H
#define SE_INCL_DRAWPORT_H
#ifdef PRAGMA_ONCE
	#pragma once
#endif

#include <Engine/Base/Lists.h>


// public drawport's text flags
#define DPTF_BOLD   (1L<<0)
#define DPTF_ITALIC (1L<<1)
// internals
#define DPTF_PRINTSPECIALCODES  (1L<<10)
#define DPTF_IGNORESPECIALCODES (1L<<11)

typedef struct _UIRectUV UIRectUV;

class ENGINE_API CDrawPort {
// implementation:
public:
	class CRaster *dp_Raster;	     // pointer to the Raster this refers to
	class CFontData *dp_FontData;  // pointer to the current text font
	// drawport box relative to the raster's origin
	PIX   dp_MinI, dp_MinJ;
	PIX   dp_MaxI, dp_MaxJ;
	// drawport scissoring box relative to the raster's origin
	PIX   dp_ScissorMinI, dp_ScissorMinJ;
	PIX   dp_ScissorMaxI, dp_ScissorMaxJ;
	PIX	  dp_pixTextCharSpacing;	 // space between chars in text
	PIX	  dp_pixTextLineSpacing;	 // space between lines in text
	FLOAT dp_fTextScaling;				 // scaling factor for font size
	FLOAT dp_fTextAspect;				   // aspect ratio for font (x/y)
	FLOAT dp_fTextShadow;          // drop-shadow direction and offset (<0=up-left, >0=down-right, 0=none)
	ULONG dp_ulTextFlags;          // see DPTF_...
	ULONG dp_ulTextBlendingType;   // prredefinedBlendType (PBT_BLEND and such) - cannot use wrapper's enum here 
	FLOAT dp_fWideAdjustment;      // for wide (16:9 or 16:10) screen support (needed in some calculations) = 1.0f or >1 (10/12 perhaps)
	BOOL  dp_bRenderingOverlay;    // set when scene renderer requires overlay mode (don't clear z-buffer)

