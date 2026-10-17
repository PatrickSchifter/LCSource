#ifndef SE_INCL_WIN32COMPAT_H
#define SE_INCL_WIN32COMPAT_H
#ifdef PRAGMA_ONCE
  #pragma once
#endif

// Outside Windows only (included by Types.h under PLATFORM_UNIX): the Win32 types, messages and
// CRT names that the engine and the UI use as plain data. The UI keeps receiving MSG structs
// (CUIManager::MsgProc); the platform layer builds them from its own events, so every constant
// here has the value it has in the Windows SDK. Only self-contained Win32/CRT functions are
// emulated here (and in Win32Compat.cpp); windows, input and files are ported where they are
// used (HANDOFF.md, "C5 — roteiro").

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <wchar.h>

// basic types (DWORD, LONG, HWND, HINSTANCE, POINT and RECT are in Types.h)
typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned char  UCHAR;
typedef unsigned short USHORT;
typedef char           CHAR;
typedef int            INT;
typedef int8_t         INT8;
typedef int16_t        INT16;
typedef int32_t        INT32;
typedef int64_t        INT64;
typedef uint8_t        UINT8;
typedef uint16_t       UINT16;
typedef uint32_t       UINT32;
typedef uint64_t       UINT64;
typedef float          FLOAT32;
typedef wchar_t        WCHAR;
typedef char           TCHAR;
#define VOID void

typedef BYTE          *PBYTE;
typedef BYTE          *LPBYTE;
typedef WORD          *LPWORD;
typedef DWORD         *LPDWORD;
typedef void          *PVOID;
typedef void          *LPVOID;
typedef const void    *LPCVOID;
typedef char          *LPSTR;
typedef const char    *LPCSTR;
typedef char          *LPTSTR;
typedef char          *PTSTR;
typedef const char    *LPCTSTR;
typedef wchar_t       *LPWSTR;
typedef const wchar_t *LPCWSTR;

typedef intptr_t  INT_PTR;
typedef uintptr_t UINT_PTR;
typedef intptr_t  LONG_PTR;
typedef uintptr_t ULONG_PTR;
typedef uintptr_t DWORD_PTR;
typedef UINT_PTR  WPARAM;
typedef LONG_PTR  LPARAM;
typedef LONG_PTR  LRESULT;
typedef LONG      HRESULT;

typedef void *HANDLE;
typedef void *HMODULE;
typedef void *HDC;
typedef void *HICON;
typedef void *HCURSOR;
typedef void *HMENU;
typedef void *HBITMAP;
typedef void *HGLOBAL;

#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define INFINITE             0xFFFFFFFF

#define WINAPI
#define APIENTRY
#define CALLBACK
#define PASCAL
#define FAR
#define NEAR
#define IN
#define OUT

#define S_OK          ((HRESULT)0)
#define S_FALSE       ((HRESULT)1)
#define E_FAIL        ((HRESULT)0x80004005)
#define E_INVALIDARG  ((HRESULT)0x80070057)
#define E_OUTOFMEMORY ((HRESULT)0x8007000E)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr)    (((HRESULT)(hr)) < 0)
#define ERROR_SUCCESS 0L
#define NOERROR       0

typedef union {
  struct { DWORD LowPart; LONG HighPart; };
  struct { DWORD LowPart; LONG HighPart; } u;
  LONGLONG QuadPart;
} LARGE_INTEGER;

typedef union {
  struct { DWORD LowPart; DWORD HighPart; };
  struct { DWORD LowPart; DWORD HighPart; } u;
  uint64_t QuadPart;
} ULARGE_INTEGER;

typedef struct {
  WORD wYear;
  WORD wMonth;
  WORD wDayOfWeek;
  WORD wDay;
  WORD wHour;
  WORD wMinute;
  WORD wSecond;
  WORD wMilliseconds;
} SYSTEMTIME;

// window message, as filled in by the platform layer
typedef struct {
  HWND   hwnd;
  UINT   message;
  WPARAM wParam;
  LPARAM lParam;
  DWORD  time;
  POINT  pt;
} MSG, *LPMSG;

#define LOWORD(l)          ((WORD)(((DWORD_PTR)(l)) & 0xFFFF))
#define HIWORD(l)          ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xFFFF))
#define LOBYTE(w)          ((BYTE)(((DWORD_PTR)(w)) & 0xFF))
#define HIBYTE(w)          ((BYTE)((((DWORD_PTR)(w)) >> 8) & 0xFF))
#define MAKEWORD(a, b)     ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))
#define MAKELONG(a, b)     ((LONG)(((WORD)(a)) | ((DWORD)((WORD)(b))) << 16))
#define MAKEWPARAM(l, h)   ((WPARAM)(DWORD)MAKELONG(l, h))
#define MAKELPARAM(l, h)   ((LPARAM)(DWORD)MAKELONG(l, h))
#define GET_X_LPARAM(lp)   ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp)   ((int)(short)HIWORD(lp))
#define GET_WHEEL_DELTA_WPARAM(wp) ((short)HIWORD(wp))
#define WHEEL_DELTA        120

#define ZeroMemory(p, n)      memset((p), 0, (n))
#define FillMemory(p, n, v)   memset((p), (v), (n))
#define CopyMemory(d, s, n)   memcpy((d), (s), (n))
#define MoveMemory(d, s, n)   memmove((d), (s), (n))

// window messages
#define WM_NULL                  0x0000
#define WM_CREATE                0x0001
#define WM_DESTROY               0x0002
#define WM_MOVE                  0x0003
#define WM_SIZE                  0x0005
#define WM_ACTIVATE              0x0006
#define WM_SETFOCUS              0x0007
#define WM_KILLFOCUS             0x0008
#define WM_PAINT                 0x000F
#define WM_CLOSE                 0x0010
#define WM_QUIT                  0x0012
#define WM_ERASEBKGND            0x0014
#define WM_SHOWWINDOW            0x0018
#define WM_ACTIVATEAPP           0x001C
#define WM_CANCELMODE            0x001F
#define WM_SETCURSOR             0x0020
#define WM_GETMINMAXINFO         0x0024
#define WM_INPUTLANGCHANGEREQUEST 0x0050
#define WM_INPUTLANGCHANGE       0x0051
#define WM_KEYFIRST              0x0100
#define WM_KEYDOWN               0x0100
#define WM_KEYUP                 0x0101
#define WM_CHAR                  0x0102
#define WM_DEADCHAR              0x0103
#define WM_SYSKEYDOWN            0x0104
#define WM_SYSKEYUP              0x0105
#define WM_SYSCHAR               0x0106
#define WM_SYSDEADCHAR           0x0107
#define WM_KEYLAST               0x0109
#define WM_IME_STARTCOMPOSITION  0x010D
#define WM_IME_ENDCOMPOSITION    0x010E
#define WM_IME_COMPOSITION       0x010F
#define WM_IME_KEYLAST           0x010F
#define WM_SYSCOMMAND            0x0112
#define WM_TIMER                 0x0113
#define WM_MOUSEFIRST            0x0200
#define WM_MOUSEMOVE             0x0200
#define WM_LBUTTONDOWN           0x0201
#define WM_LBUTTONUP             0x0202
#define WM_LBUTTONDBLCLK         0x0203
#define WM_RBUTTONDOWN           0x0204
#define WM_RBUTTONUP             0x0205
#define WM_RBUTTONDBLCLK         0x0206
#define WM_MBUTTONDOWN           0x0207
#define WM_MBUTTONUP             0x0208
#define WM_MBUTTONDBLCLK         0x0209
#define WM_MOUSEWHEEL            0x020A
#define WM_XBUTTONDOWN           0x020B
#define WM_XBUTTONUP             0x020C
#define WM_XBUTTONDBLCLK         0x020D
#define WM_MOUSEHWHEEL           0x020E
#define WM_MOUSELAST             0x020E
#define WM_PARENTNOTIFY          0x0210
#define WM_POWERBROADCAST        0x0218
#define WM_EXITSIZEMOVE          0x0232
#define WM_IME_SETCONTEXT        0x0281
#define WM_IME_NOTIFY            0x0282
#define WM_IME_CONTROL           0x0283
#define WM_IME_COMPOSITIONFULL   0x0284
#define WM_IME_SELECT            0x0285
#define WM_IME_CHAR              0x0286
#define WM_IME_REQUEST           0x0288
#define WM_IME_KEYDOWN           0x0290
#define WM_IME_KEYUP             0x0291
#define WM_USER                  0x0400
#define WM_APP                   0x8000

// WM_ACTIVATE
#define WA_INACTIVE    0
#define WA_ACTIVE      1
#define WA_CLICKACTIVE 2

// WM_SYSCOMMAND
#define SC_MINIMIZE     0xF020
#define SC_MAXIMIZE     0xF030
#define SC_CLOSE        0xF060
#define SC_KEYMENU      0xF100
#define SC_RESTORE      0xF120
#define SC_SCREENSAVE   0xF140
#define SC_MONITORPOWER 0xF170

// mouse message key state (wParam)
#define MK_LBUTTON  0x0001
#define MK_RBUTTON  0x0002
#define MK_SHIFT    0x0004
#define MK_CONTROL  0x0008
#define MK_MBUTTON  0x0010
#define MK_XBUTTON1 0x0020
#define MK_XBUTTON2 0x0040

// virtual keys
#define VK_LBUTTON    0x01
#define VK_RBUTTON    0x02
#define VK_CANCEL     0x03
#define VK_MBUTTON    0x04
#define VK_XBUTTON1   0x05
#define VK_XBUTTON2   0x06
#define VK_BACK       0x08
#define VK_TAB        0x09
#define VK_CLEAR      0x0C
#define VK_RETURN     0x0D
#define VK_SHIFT      0x10
#define VK_CONTROL    0x11
#define VK_MENU       0x12
#define VK_PAUSE      0x13
#define VK_CAPITAL    0x14
#define VK_KANA       0x15
#define VK_HANGUL     0x15
#define VK_JUNJA      0x17
#define VK_FINAL      0x18
#define VK_HANJA      0x19
#define VK_KANJI      0x19
#define VK_ESCAPE     0x1B
#define VK_CONVERT    0x1C
#define VK_NONCONVERT 0x1D
#define VK_SPACE      0x20
#define VK_PRIOR      0x21
#define VK_NEXT       0x22
#define VK_END        0x23
#define VK_HOME       0x24
#define VK_LEFT       0x25
#define VK_UP         0x26
#define VK_RIGHT      0x27
#define VK_DOWN       0x28
#define VK_SELECT     0x29
#define VK_PRINT      0x2A
#define VK_EXECUTE    0x2B
#define VK_SNAPSHOT   0x2C
#define VK_INSERT     0x2D
#define VK_DELETE     0x2E
#define VK_HELP       0x2F
#define VK_LWIN       0x5B
#define VK_RWIN       0x5C
#define VK_APPS       0x5D
#define VK_SLEEP      0x5F
#define VK_NUMPAD0    0x60
#define VK_NUMPAD1    0x61
#define VK_NUMPAD2    0x62
#define VK_NUMPAD3    0x63
#define VK_NUMPAD4    0x64
#define VK_NUMPAD5    0x65
#define VK_NUMPAD6    0x66
#define VK_NUMPAD7    0x67
#define VK_NUMPAD8    0x68
#define VK_NUMPAD9    0x69
#define VK_MULTIPLY   0x6A
#define VK_ADD        0x6B
#define VK_SEPARATOR  0x6C
#define VK_SUBTRACT   0x6D
#define VK_DECIMAL    0x6E
#define VK_DIVIDE     0x6F
#define VK_F1         0x70
#define VK_F2         0x71
#define VK_F3         0x72
#define VK_F4         0x73
#define VK_F5         0x74
#define VK_F6         0x75
#define VK_F7         0x76
#define VK_F8         0x77
#define VK_F9         0x78
#define VK_F10        0x79
#define VK_F11        0x7A
#define VK_F12        0x7B
#define VK_NUMLOCK    0x90
#define VK_SCROLL     0x91
#define VK_LSHIFT     0xA0
#define VK_RSHIFT     0xA1
#define VK_LCONTROL   0xA2
#define VK_RCONTROL   0xA3
#define VK_LMENU      0xA4
#define VK_RMENU      0xA5
#define VK_OEM_1      0xBA
#define VK_OEM_PLUS   0xBB
#define VK_OEM_COMMA  0xBC
#define VK_OEM_MINUS  0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2      0xBF
#define VK_OEM_3      0xC0
#define VK_OEM_4      0xDB
#define VK_OEM_5      0xDC
#define VK_OEM_6      0xDD
#define VK_OEM_7      0xDE
#define VK_OEM_8      0xDF
#define VK_OEM_102    0xE2
#define VK_PROCESSKEY 0xE5

// ShowWindow
#define SW_HIDE       0
#define SW_SHOWNORMAL 1
#define SW_MINIMIZE   6
#define SW_SHOW       5

// MessageBox
#define MB_OK              0x00000000L
#define MB_OKCANCEL        0x00000001L
#define MB_YESNO           0x00000004L
#define MB_ICONHAND        0x00000010L
#define MB_ICONERROR       MB_ICONHAND
#define MB_ICONSTOP        MB_ICONHAND
#define MB_ICONQUESTION    0x00000020L
#define MB_ICONEXCLAMATION 0x00000030L
#define MB_ICONWARNING     MB_ICONEXCLAMATION
#define MB_ICONINFORMATION 0x00000040L
#define MB_ICONASTERISK    MB_ICONINFORMATION
#define MB_SYSTEMMODAL     0x00001000L
#define MB_TASKMODAL       0x00002000L
#define MB_SETFOREGROUND   0x00010000L
#define MB_TOPMOST         0x00040000L
#define IDOK     1
#define IDCANCEL 2
#define IDYES    6
#define IDNO     7

// code pages (MultiByteToWideChar...)
#define CP_ACP  0
#define CP_UTF8 65001

// CRT names
#define _MAX_PATH 260
#define _T(x) x
#define __min(a, b) (((a) < (b)) ? (a) : (b))
#define __max(a, b) (((a) > (b)) ? (a) : (b))

#define stricmp    strcasecmp
#define _stricmp   strcasecmp
#define strcmpi    strcasecmp
#define _strcmpi   strcasecmp
#define strnicmp   strncasecmp
#define _strnicmp  strncasecmp
#define wcsicmp    wcscasecmp
#define _wcsicmp   wcscasecmp
#define _snprintf  snprintf
#define _vsnprintf vsnprintf
#define _atoi64(s) strtoll((s), NULL, 10)
#define _finite(x) isfinite(x)

inline char *_strupr(char *str) { for (char *p = str; *p; p++) *p = (char)toupper((unsigned char)*p); return str; }
inline char *_strlwr(char *str) { for (char *p = str; *p; p++) *p = (char)tolower((unsigned char)*p); return str; }
#define strupr _strupr
#define strlwr _strlwr

inline char *_ltoa(long lValue, char *str, int iRadix)
{
  // as in the CRT: negative numbers get a sign only in base 10, other bases show the bits
  char achDigits[66];
  int ct = 0;
  unsigned long ulValue = (iRadix == 10 && lValue < 0) ? 0UL - (unsigned long)lValue : (unsigned long)lValue;
  if (iRadix != 10) {
    ulValue &= 0xFFFFFFFFUL;  // a 32-bit long, as on Windows
  }
  do {
    const int iDigit = (int)(ulValue % iRadix);
    achDigits[ct++] = (char)(iDigit < 10 ? '0' + iDigit : 'a' + iDigit - 10);
    ulValue /= iRadix;
  } while (ulValue != 0);
  char *pch = str;
  if (iRadix == 10 && lValue < 0) {
    *pch++ = '-';
  }
  while (ct > 0) {
    *pch++ = achDigits[--ct];
  }
  *pch = 0;
  return str;
}
inline char *_itoa(int iValue, char *str, int iRadix) { return _ltoa(iValue, str, iRadix); }
#define itoa _itoa
#define ltoa _ltoa

inline int strcpy_s(char *strDest, size_t ctDest, const char *strSrc)
{
  const size_t ctSrc = strlen(strSrc);
  if (ctSrc >= ctDest) {
    if (ctDest > 0) strDest[0] = 0;
    return 34;  // ERANGE
  }
  memcpy(strDest, strSrc, ctSrc + 1);
  return 0;
}
template<size_t ctDest>
inline int strcpy_s(char (&strDest)[ctDest], const char *strSrc) { return strcpy_s(strDest, ctDest, strSrc); }

// copies at most ctCount characters and always terminates; what does not fit is an error, as in
// the CRT (which would also call its invalid parameter handler)
inline int strncpy_s(char *strDest, size_t ctDest, const char *strSrc, size_t ctCount)
{
  size_t ctSrc = strnlen(strSrc, ctCount);
  if (ctSrc >= ctDest) {
    if (ctDest > 0) strDest[0] = 0;
    return 34;  // ERANGE
  }
  memcpy(strDest, strSrc, ctSrc);
  strDest[ctSrc] = 0;
  return 0;
}
template<size_t ctDest>
inline int strncpy_s(char (&strDest)[ctDest], const char *strSrc, size_t ctCount) { return strncpy_s(strDest, ctDest, strSrc, ctCount); }

inline int sprintf_s(char *str, size_t ctDest, const char *strFormat, ...)
{
  va_list arg;
  va_start(arg, strFormat);
  const int iResult = vsnprintf(str, ctDest, strFormat, arg);
  va_end(arg);
  return iResult;
}
template<size_t ctDest>
inline int sprintf_s(char (&str)[ctDest], const char *strFormat, ...)
{
  va_list arg;
  va_start(arg, strFormat);
  const int iResult = vsnprintf(str, ctDest, strFormat, arg);
  va_end(arg);
  return iResult;
}

#define wsprintf    sprintf
#define lstrcpy     strcpy
#define _tcslen     strlen
#define _vsntprintf vsnprintf

// debug CRT (no-ops in a release CRT as well)
#define _CrtCheckMemory() TRUE
#define _RPT0(type, msg)                     ((void)0)
#define _RPT1(type, msg, a1)                 ((void)0)
#define _RPT2(type, msg, a1, a2)             ((void)0)
#define _RPT3(type, msg, a1, a2, a3)         ((void)0)
#define _RPT4(type, msg, a1, a2, a3, a4)     ((void)0)
#define _ASSERT(expr) ASSERT(expr)

inline void OutputDebugString(const char *str) { fputs(str, stderr); }
#define OutputDebugStringA OutputDebugString

// size of a heap block (Memory.cpp, MemoryTracking.cpp)
#ifdef __APPLE__
  #include <malloc/malloc.h>
  inline size_t _msize(void *pv) { return malloc_size(pv); }
#endif

// GlobalMemoryStatus (Win32Compat.cpp): sizes as a 32-bit Windows process sees them, clamped to
// 32 bits; page file and virtual memory are not reported (zero)
typedef struct {
  DWORD dwLength;
  DWORD dwMemoryLoad;
  DWORD dwTotalPhys;
  DWORD dwAvailPhys;
  DWORD dwTotalPageFile;
  DWORD dwAvailPageFile;
  DWORD dwTotalVirtual;
  DWORD dwAvailVirtual;
} MEMORYSTATUS, *LPMEMORYSTATUS;
void GlobalMemoryStatus(LPMEMORYSTATUS pms);

#endif  /* include-once check. */
