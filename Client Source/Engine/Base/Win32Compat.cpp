#include "stdh.h"

// Win32 functions emulated outside Windows (declared in Win32Compat.h). Compiled empty on Windows.
#ifndef _WIN32

#include <sys/types.h>
#include <sys/sysctl.h>
#ifdef __APPLE__
  #include <mach/mach.h>
#endif
#include <time.h>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

/////////////////////////////////////////////////////////////////////
// time

DWORD timeGetTime(void)
{
  static const std::chrono::steady_clock::time_point tpStart = std::chrono::steady_clock::now();
  const std::chrono::steady_clock::duration d = std::chrono::steady_clock::now() - tpStart;
  return (DWORD)std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

void Sleep(DWORD dwMilliseconds)
{
  std::this_thread::sleep_for(std::chrono::milliseconds(dwMilliseconds));
}

BOOL QueryPerformanceCounter(LARGE_INTEGER *pliCount)
{
  pliCount->QuadPart = std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
  return TRUE;
}

BOOL QueryPerformanceFrequency(LARGE_INTEGER *pliFrequency)
{
  pliFrequency->QuadPart = 1000000000LL;
  return TRUE;
}

char *_strtime(char *strTime)
{
  time_t t = time(NULL);
  struct tm tmLocal;
  localtime_r(&t, &tmLocal);
  strftime(strTime, 9, "%H:%M:%S", &tmLocal);
  return strTime;
}

char *_strdate(char *strDate)
{
  time_t t = time(NULL);
  struct tm tmLocal;
  localtime_r(&t, &tmLocal);
  strftime(strDate, 9, "%m/%d/%y", &tmLocal);
  return strDate;
}

/////////////////////////////////////////////////////////////////////
// multimedia timers: a thread per timer; callbacks are serialized

namespace {

struct MMTimer {
  UINT uID;
  std::chrono::milliseconds msDelay;
  LPTIMECALLBACK pCallback;
  DWORD_PTR dwUser;
  BOOL bPeriodic;
  std::thread thread;
  std::mutex mx;
  std::condition_variable cv;
  BOOL bStop = FALSE;
};

std::mutex _mxTimers;          // guards _mapTimers and _uNextTimerID
std::recursive_mutex _mxCallbacks;  // one callback at a time
std::map<UINT, std::shared_ptr<MMTimer>> _mapTimers;
UINT _uNextTimerID = 1;

void TimerThread(std::shared_ptr<MMTimer> ptm)
{
  std::chrono::steady_clock::time_point tpNext = std::chrono::steady_clock::now() + ptm->msDelay;
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(ptm->mx);
      if (ptm->cv.wait_until(lock, tpNext, [&] { return ptm->bStop != FALSE; })) {
        return;
      }
    }
    {
      std::lock_guard<std::recursive_mutex> lockCallbacks(_mxCallbacks);
      ptm->pCallback(ptm->uID, 0, ptm->dwUser, 0, 0);
    }
    if (!ptm->bPeriodic) {
      // a one-shot timer is gone once it fires
      std::lock_guard<std::mutex> lockTimers(_mxTimers);
      std::map<UINT, std::shared_ptr<MMTimer>>::iterator it = _mapTimers.find(ptm->uID);
      if (it != _mapTimers.end() && it->second == ptm) {
        ptm->thread.detach();
        _mapTimers.erase(it);
      }
      return;
    }
    // periodic: keep the original schedule, as winmm does
    tpNext += ptm->msDelay;
  }
}

}  // namespace

MMRESULT timeSetEvent(UINT uDelay, UINT uResolution, LPTIMECALLBACK lpTimeProc, DWORD_PTR dwUser, UINT fuEvent)
{
  (void)uResolution;
  std::shared_ptr<MMTimer> ptm = std::make_shared<MMTimer>();
  ptm->msDelay = std::chrono::milliseconds(uDelay);
  ptm->pCallback = lpTimeProc;
  ptm->dwUser = dwUser;
  ptm->bPeriodic = (fuEvent & TIME_PERIODIC) != 0;

  std::lock_guard<std::mutex> lockTimers(_mxTimers);
  ptm->uID = _uNextTimerID++;
  if (_uNextTimerID == 0) _uNextTimerID = 1;
  _mapTimers[ptm->uID] = ptm;
  // the thread may run (and a one-shot may try to remove itself) only after this lock is released
  ptm->thread = std::thread(TimerThread, ptm);
  return ptm->uID;
}

MMRESULT timeKillEvent(UINT uTimerID)
{
  std::shared_ptr<MMTimer> ptm;
  {
    std::lock_guard<std::mutex> lockTimers(_mxTimers);
    std::map<UINT, std::shared_ptr<MMTimer>>::iterator it = _mapTimers.find(uTimerID);
    if (it == _mapTimers.end()) {
      return MMSYSERR_INVALPARAM;
    }
    ptm = it->second;
    _mapTimers.erase(it);
  }
  {
    std::lock_guard<std::mutex> lock(ptm->mx);
    ptm->bStop = TRUE;
  }
  ptm->cv.notify_all();
  // killed from its own callback: the thread ends by itself after the callback returns
  if (ptm->thread.get_id() == std::this_thread::get_id()) {
    ptm->thread.detach();
  } else if (ptm->thread.joinable()) {
    ptm->thread.join();
  }
  return TIMERR_NOERROR;
}

/////////////////////////////////////////////////////////////////////
// memory

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
