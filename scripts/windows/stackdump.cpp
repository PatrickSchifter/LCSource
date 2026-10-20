// Prints a symbolized stack for every thread of a 32-bit process: stackdump.exe <pid>
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#pragma comment(lib, "dbghelp.lib")

int main(int argc, char** argv) {
  if (argc < 2) { printf("usage: stackdump <pid>\n"); return 2; }
  DWORD pid = (DWORD)atoi(argv[1]);
  HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
  if (!hProc) { printf("OpenProcess failed %lu\n", GetLastError()); return 1; }
  SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  SymInitialize(hProc, NULL, TRUE);
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  THREADENTRY32 te = { sizeof(te) };
  for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
    if (te.th32OwnerProcessID != pid) continue;
    HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
    if (!hThread) continue;
    SuspendThread(hThread);
    CONTEXT ctx = {}; ctx.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(hThread, &ctx)) {
      printf("=== thread %lu\n", te.th32ThreadID);
      STACKFRAME64 sf = {};
      sf.AddrPC.Offset = ctx.Eip; sf.AddrPC.Mode = AddrModeFlat;
      sf.AddrFrame.Offset = ctx.Ebp; sf.AddrFrame.Mode = AddrModeFlat;
      sf.AddrStack.Offset = ctx.Esp; sf.AddrStack.Mode = AddrModeFlat;
      for (int i = 0; i < 40 && StackWalk64(IMAGE_FILE_MACHINE_I386, hProc, hThread, &sf, &ctx, NULL,
                                             SymFunctionTableAccess64, SymGetModuleBase64, NULL); i++) {
        char buf[sizeof(SYMBOL_INFO) + 256] = {}; SYMBOL_INFO* sym = (SYMBOL_INFO*)buf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO); sym->MaxNameLen = 255;
        DWORD64 disp = 0; DWORD ldisp = 0; IMAGEHLP_LINE64 line = { sizeof(line) };
        BOOL hasSym = SymFromAddr(hProc, sf.AddrPC.Offset, &disp, sym);
        BOOL hasLine = SymGetLineFromAddr64(hProc, sf.AddrPC.Offset, &ldisp, &line);
        printf("  %08llX %s", sf.AddrPC.Offset, hasSym ? sym->Name : "?");
        if (hasLine) printf("  (%s:%lu)", line.FileName, line.LineNumber);
        printf("\n");
      }
    }
    ResumeThread(hThread);
    CloseHandle(hThread);
  }
  CloseHandle(snap);
  return 0;
}
