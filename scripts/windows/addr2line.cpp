// addr2line <module.dll> <section-1 offset hex>...: prints function and file:line from the PDB.
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#pragma comment(lib, "dbghelp.lib")

int main(int argc, char** argv) {
  if (argc < 3) { printf("usage: addr2line <dll> <offset>...\n"); return 2; }
  HANDLE h = GetCurrentProcess();
  SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
  SymInitialize(h, NULL, FALSE);
  const DWORD64 base = 0x10000000;
  if (!SymLoadModuleEx(h, NULL, argv[1], NULL, base, 0, NULL, 0)) { printf("load failed %lu\n", GetLastError()); return 1; }
  for (int i = 2; i < argc; i++) {
    DWORD64 addr = base + 0x1000 + strtoull(argv[i], NULL, 16);  // .text is the first section at RVA 0x1000
    char buf[sizeof(SYMBOL_INFO) + 512] = {};
    SYMBOL_INFO* sym = (SYMBOL_INFO*)buf; sym->SizeOfStruct = sizeof(SYMBOL_INFO); sym->MaxNameLen = 511;
    DWORD64 disp = 0; DWORD ldisp = 0; IMAGEHLP_LINE64 line = { sizeof(line) };
    const char* name = SymFromAddr(h, addr, &disp, sym) ? sym->Name : "?";
    if (SymGetLineFromAddr64(h, addr, &ldisp, &line)) printf("%s  %s:%lu\n", name, line.FileName, line.LineNumber);
    else printf("%s +0x%llX\n", name, disp);
  }
  return 0;
}
