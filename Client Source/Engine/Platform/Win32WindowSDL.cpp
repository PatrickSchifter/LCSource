#include "stdh.h"

// The user32 window, cursor and screen functions the engine and the UI call, over SDL3, outside
// Windows (declared in Win32Compat.h). An HWND is an SDL_Window (Platform.h). Compiled empty on
// Windows, where the real functions work on the window SDL creates.
#ifndef PLATFORM_WIN32

#include <Engine/Platform/Platform.h>
#include <Engine/Util.h>
#include <SDL3/SDL.h>
#include <math.h>

static SDL_Window *SDLWindow(HWND hWnd)
{
  return (SDL_Window*)hWnd;
}

BOOL GetClientRect(HWND hWnd, RECT *prc)
{
  int iWidth = 0, iHeight = 0;
  if (hWnd == NULL || !SDL_GetWindowSize(SDLWindow(hWnd), &iWidth, &iHeight)) {
    SetRectEmpty(prc);
    return FALSE;
  }
  return SetRect(prc, 0, 0, iWidth, iHeight);
}

BOOL GetWindowRect(HWND hWnd, RECT *prc)
{
  int iX = 0, iY = 0, iWidth = 0, iHeight = 0;
  if (hWnd == NULL || !SDL_GetWindowPosition(SDLWindow(hWnd), &iX, &iY)) {
    SetRectEmpty(prc);
    return FALSE;
  }
  SDL_GetWindowSize(SDLWindow(hWnd), &iWidth, &iHeight);
  // with the frame, as on Windows
  int iTop = 0, iLeft = 0, iBottom = 0, iRight = 0;
  SDL_GetWindowBordersSize(SDLWindow(hWnd), &iTop, &iLeft, &iBottom, &iRight);
  return SetRect(prc, iX - iLeft, iY - iTop, iX + iWidth + iRight, iY + iHeight + iBottom);
}

BOOL ClientToScreen(HWND hWnd, POINT *ppt)
{
  int iX = 0, iY = 0;
  if (hWnd == NULL || !SDL_GetWindowPosition(SDLWindow(hWnd), &iX, &iY)) {
    return FALSE;
  }
  ppt->x += iX;
  ppt->y += iY;
  return TRUE;
}

BOOL ScreenToClient(HWND hWnd, POINT *ppt)
{
  int iX = 0, iY = 0;
  if (hWnd == NULL || !SDL_GetWindowPosition(SDLWindow(hWnd), &iX, &iY)) {
    return FALSE;
  }
  ppt->x -= iX;
  ppt->y -= iY;
  return TRUE;
}

BOOL IsWindow(HWND hWnd)
{
  return hWnd != NULL && SDL_GetWindowID(SDLWindow(hWnd)) != 0;
}

BOOL IsIconic(HWND hWnd)
{
  return hWnd != NULL && (SDL_GetWindowFlags(SDLWindow(hWnd)) & SDL_WINDOW_MINIMIZED) != 0;
}

BOOL ShowWindow(HWND hWnd, int nCmdShow)
{
  if (hWnd == NULL) {
    return FALSE;
  }
  SDL_Window *pwnd = SDLWindow(hWnd);
  const BOOL bWasVisible = (SDL_GetWindowFlags(pwnd) & SDL_WINDOW_HIDDEN) == 0;
  switch (nCmdShow) {
  case SW_HIDE:
    SDL_HideWindow(pwnd);
    break;
  case SW_MINIMIZE:
    SDL_MinimizeWindow(pwnd);
    break;
  case SW_SHOWNORMAL:
    SDL_RestoreWindow(pwnd);
    SDL_ShowWindow(pwnd);
    break;
  default:
    SDL_ShowWindow(pwnd);
    break;
  }
  return bWasVisible;
}

BOOL UpdateWindow(HWND hWnd)
{
  return hWnd != NULL;
}

HWND SetFocus(HWND hWnd)
{
  HWND hWndOld = GetFocus();
  if (hWnd != NULL) {
    SDL_RaiseWindow(SDLWindow(hWnd));
  }
  return hWndOld;
}

HWND GetFocus(void)
{
  return (HWND)SDL_GetKeyboardFocus();
}

BOOL SetWindowText(HWND hWnd, const char *strText)
{
  if (hWnd == NULL) {
    return FALSE;
  }
  StackMultiByteToWideChar(CP_ACP, strText, -1, wcsText);
  return SetWindowTextW(hWnd, wcsText != NULL ? wcsText : L"");
}

BOOL SetWindowTextW(HWND hWnd, const wchar_t *strText)
{
  if (hWnd == NULL) {
    return FALSE;
  }
  StackWideCharToMultiByte(CP_UTF8, strText, -1, strUTF8);
  return SDL_SetWindowTitle(SDLWindow(hWnd), strUTF8 != NULL ? strUTF8 : "");
}

BOOL GetCursorPos(POINT *ppt)
{
  float fX = 0.0f, fY = 0.0f;
  SDL_GetGlobalMouseState(&fX, &fY);
  ppt->x = (LONG)floorf(fX);
  ppt->y = (LONG)floorf(fY);
  return TRUE;
}

BOOL SetCursorPos(int x, int y)
{
  return SDL_WarpMouseGlobal((float)x, (float)y);
}

BOOL ClipCursor(const RECT *prc)
{
  SDL_Window *pwnd = PLT_GetMainWindow();
  if (pwnd == NULL) {
    return FALSE;
  }
  if (prc == NULL) {
    return SDL_SetWindowMouseRect(pwnd, NULL);
  }
  // SDL confines the cursor to a rectangle of the window's client area
  POINT ptOrigin = { 0, 0 };
  ClientToScreen((HWND)pwnd, &ptOrigin);
  SDL_Rect rect;
  rect.x = prc->left - ptOrigin.x;
  rect.y = prc->top - ptOrigin.y;
  rect.w = prc->right - prc->left;
  rect.h = prc->bottom - prc->top;
  return SDL_SetWindowMouseRect(pwnd, &rect);
}

int ShowCursor(BOOL bShow)
{
  // the cursor shows while the counter is not negative
  static int _iDisplayCount = 0;
  _iDisplayCount += bShow ? 1 : -1;
  if (_iDisplayCount >= 0) {
    SDL_ShowCursor();
  } else {
    SDL_HideCursor();
  }
  return _iDisplayCount;
}

HWND SetCapture(HWND hWnd)
{
  SDL_CaptureMouse(hWnd != NULL);
  return NULL;
}

BOOL ReleaseCapture(void)
{
  return SDL_CaptureMouse(false);
}

SHORT GetAsyncKeyState(int iVirtKey)
{
  return PLT_IsVirtualKeyDown((UINT)iVirtKey) ? (SHORT)0x8000 : 0;
}

SHORT GetKeyState(int iVirtKey)
{
  SHORT sState = GetAsyncKeyState(iVirtKey);
  const SDL_Keymod mod = SDL_GetModState();
  if ((iVirtKey == VK_CAPITAL && (mod & SDL_KMOD_CAPS))
   || (iVirtKey == VK_NUMLOCK && (mod & SDL_KMOD_NUM))
   || (iVirtKey == VK_SCROLL && (mod & SDL_KMOD_SCROLL))) {
    sState |= 1;
  }
  return sState;
}

int GetSystemMetrics(int nIndex)
{
  switch (nIndex) {
  case SM_CXSCREEN:
  case SM_CYSCREEN: {
    PIX pixWidth, pixHeight;
    PLT_GetDesktopSize(pixWidth, pixHeight);
    return nIndex == SM_CXSCREEN ? pixWidth : pixHeight;
  }
  case SM_CMONITORS: {
    int ctDisplays = 0;
    SDL_DisplayID *paid = SDL_GetDisplays(&ctDisplays);
    SDL_free(paid);
    return ctDisplays;
  }
  default:
    return 0;
  }
}

#endif  // !PLATFORM_WIN32
