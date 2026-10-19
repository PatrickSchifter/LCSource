#ifndef SE_INCL_METALDEVICE_H
#define SE_INCL_METALDEVICE_H
#ifdef PRAGMA_ONCE
  #pragma once
#endif

// The Metal device (MetalDevice.mm, Objective-C++) under the engine's Metal backend (GAT_MTL):
// a GpuDevice (GpuDevice.h), drawn through by Gfx_wrapper_Gpu.cpp.

#include "GpuDevice.h"

// whether the system has a Metal device
bool mtlIsAvailable(void);
const GpuDevice *mtlDevice(void);

#endif  /* include-once check. */
