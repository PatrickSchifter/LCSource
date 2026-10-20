// Definitions the MinGW-w64 (Wine) d3d8.h, d3d8caps.h and d3d8types.h expect from the
// MinGW runtime, plus constants they omit. Force-included into every client source.
#pragma once

#ifndef WINBOOL
#define WINBOOL BOOL
#endif
#ifndef __MSABI_LONG
#define __MSABI_LONG(x) x##l
#endif
#ifndef D3DSGR_NO_CALIBRATION
#define D3DSGR_NO_CALIBRATION 0x00000000L
#define D3DSGR_CALIBRATE 0x00000001L
#endif
