
#ifdef _WIN32
class CSetPriority {
public:
  DWORD sp_dwProcessOld;
  int sp_iThreadOld;
  HANDLE sp_hThread;
  HANDLE sp_hProcess;
  CSetPriority(DWORD dwProcess, int iThread)
  {
    sp_hProcess = GetCurrentProcess();
    sp_hThread = GetCurrentThread();

    sp_dwProcessOld = GetPriorityClass(sp_hProcess);
    sp_iThreadOld = GetThreadPriority(sp_hThread);
    BOOL bSuccessProcess = SetPriorityClass(sp_hProcess, dwProcess);
    BOOL bSuccessThread = SetThreadPriority(sp_hThread, iThread);
    ASSERT(bSuccessProcess && bSuccessThread);
  }
  ~CSetPriority(void)
  {
    BOOL bSuccessProcess = SetPriorityClass(sp_hProcess, sp_dwProcessOld);
    BOOL bSuccessThread = SetThreadPriority(sp_hThread, sp_iThreadOld);
    ASSERT(bSuccessProcess && bSuccessThread);
  }
};
#else
// raising process and thread priority is Windows-only (and mostly meant for Win9x timing)
#define REALTIME_PRIORITY_CLASS       0x00000100
#define THREAD_PRIORITY_TIME_CRITICAL 15
class CSetPriority {
public:
  CSetPriority(DWORD dwProcess, int iThread) { (void)dwProcess; (void)iThread; }
};
#endif
