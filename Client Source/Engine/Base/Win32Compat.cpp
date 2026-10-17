#include "stdh.h"

// Win32 functions emulated outside Windows (declared in Win32Compat.h). Compiled empty on Windows.
#ifndef _WIN32

#include <sys/types.h>
#include <sys/sysctl.h>
#ifdef __APPLE__
  #include <mach/mach.h>
#endif

static DWORD ClampToDWORD(uint64_t ullValue)
{
  return ullValue > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : (DWORD)ullValue;
}

void GlobalMemoryStatus(LPMEMORYSTATUS pms)
{
  memset(pms, 0, sizeof(*pms));
  pms->dwLength = sizeof(*pms);

  uint64_t ullTotal = 0;
  uint64_t ullAvail = 0;
#ifdef __APPLE__
  size_t ctSize = sizeof(ullTotal);
  sysctlbyname("hw.memsize", &ullTotal, &ctSize, NULL, 0);
  // free and inactive (reclaimable) pages, as Activity Monitor counts available memory
  vm_statistics64_data_t vms;
  mach_msg_type_number_t ctInfo = HOST_VM_INFO64_COUNT;
  if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vms, &ctInfo) == KERN_SUCCESS) {
    ullAvail = (uint64_t)(vms.free_count + vms.inactive_count) * (uint64_t)vm_kernel_page_size;
  }
#endif
  if (ullAvail > ullTotal) {
    ullAvail = ullTotal;
  }
  pms->dwMemoryLoad = ullTotal > 0 ? (DWORD)((ullTotal - ullAvail) * 100 / ullTotal) : 0;
  pms->dwTotalPhys = ClampToDWORD(ullTotal);
  pms->dwAvailPhys = ClampToDWORD(ullAvail);
}

#endif  // !_WIN32
