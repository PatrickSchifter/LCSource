#ifndef SE_INCL_VULKANDEVICE_H
#define SE_INCL_VULKANDEVICE_H
#ifdef PRAGMA_ONCE
  #pragma once
#endif

// The Vulkan device (VulkanDevice.mm, Objective-C++ for the window's layer; MoltenVK on macOS)
// under the engine's Vulkan backend (GAT_VK): a GpuDevice (GpuDevice.h), drawn through by
// Gfx_wrapper_Gpu.cpp.

#include "GpuDevice.h"

// whether the system has a Vulkan device that can draw on a window
bool vlkIsAvailable(void);
const GpuDevice *vlkDevice(void);

#endif  /* include-once check. */
