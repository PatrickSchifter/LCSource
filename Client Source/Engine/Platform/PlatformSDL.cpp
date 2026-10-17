#include "stdh.h"

#include <Engine/Platform/Platform.h>

#ifdef LC_PLATFORM_SDL

#include <Engine/Base/Console.h>
#include <Engine/Base/ErrorReporting.h>
#include <Engine/Base/Translation.h>
#include <Engine/Util.h>
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED  // the client has its own main()
#include <SDL3/SDL_main.h>
#include <deque>
#include <math.h>

static SDL_Window *_pwndMain = NULL;
static std::deque<MSG> _dqMessages;  // messages translated from events, not taken yet
static float _fWheelRemainder = 0.0f;  // fraction of a wheel notch not reported yet

BOOL PLT_Init(void)
{
  // the client has its own main(); SDL does not need to run it
  SDL_SetMainReady();
  if (!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_GAMEPAD)) {
    CPrintF("SDL_Init failed: %s\n", SDL_GetError());
    return FALSE;
  }
  return TRUE;
}

void PLT_End(void)
{
  PLT_DestroyMainWindow();
  SDL_Quit();
}

// the handle the engine keeps for a window
static HWND WindowHandle(SDL_Window *pwnd)
{
#ifdef PLATFORM_WIN32
  return (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(pwnd), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
#else
  return (HWND)pwnd;
#endif
}

HWND PLT_CreateMainWindow(const char *strTitle)
{
  ASSERT(_pwndMain == NULL);
  // the client area size is set by PLT_SetMainWindowMode(); one window point is one pixel
  // (no high pixel density yet)
  _pwndMain = SDL_CreateWindow(strTitle, 640, 480, SDL_WINDOW_HIDDEN);
  if (_pwndMain == NULL) {
    FatalError(TRANS("Cannot open main window!\n%s"), SDL_GetError());
  }
  // as on Windows, every window gets text (WM_CHAR) without asking for it
  SDL_StartTextInput(_pwndMain);
  return WindowHandle(_pwndMain);
}

void PLT_DestroyMainWindow(void)
{
  if (_pwndMain != NULL) {
    SDL_StopTextInput(_pwndMain);
    SDL_DestroyWindow(_pwndMain);
    _pwndMain = NULL;
  }
  _dqMessages.clear();
}

SDL_Window *PLT_GetMainWindow(void)
{
  return _pwndMain;
}

void PLT_SetMainWindowMode(PIX pixWidth, PIX pixHeight, BOOL bFullScreen, INDEX iLeft, INDEX iTop)
{
  ASSERT(_pwndMain != NULL);
  if (bFullScreen) {
    // the display mode closest to the requested size
    SDL_DisplayMode dm;
    const SDL_DisplayID idDisplay = SDL_GetDisplayForWindow(_pwndMain);
    if (SDL_GetClosestFullscreenDisplayMode(idDisplay, pixWidth, pixHeight, 0.0f, false, &dm)) {
      SDL_SetWindowFullscreenMode(_pwndMain, &dm);
    } else {
      SDL_SetWindowFullscreenMode(_pwndMain, NULL);
    }
    SDL_SetWindowSize(_pwndMain, pixWidth, pixHeight);
    SDL_SetWindowFullscreen(_pwndMain, true);
  } else {
    SDL_SetWindowFullscreen(_pwndMain, false);
    SDL_SetWindowSize(_pwndMain, pixWidth, pixHeight);
    SDL_SetWindowPosition(_pwndMain,
      iLeft >= 0 ? iLeft : (int)SDL_WINDOWPOS_CENTERED,
      iTop >= 0 ? iTop : (int)SDL_WINDOWPOS_CENTERED);
  }
  SDL_ShowWindow(_pwndMain);
  SDL_SyncWindow(_pwndMain);
}

void PLT_SetMainWindowTitle(const char *strTitle)
{
  if (_pwndMain != NULL) {
    SDL_SetWindowTitle(_pwndMain, strTitle);
  }
}

BOOL PLT_IsMainWindowMinimized(void)
{
  return _pwndMain != NULL && (SDL_GetWindowFlags(_pwndMain) & SDL_WINDOW_MINIMIZED) != 0;
}

void PLT_GetDesktopSize(PIX &pixWidth, PIX &pixHeight)
{
  const SDL_DisplayMode *pdm = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
  pixWidth = pdm != NULL ? pdm->w : 1024;
  pixHeight = pdm != NULL ? pdm->h : 768;
}

/////////////////////////////////////////////////////////////////////
// events -> messages

// the cursor in screen coordinates, as GetCursorPos gives it
static POINT ScreenCursorPos(void)
{
  POINT pt;
#ifdef PLATFORM_WIN32
  GetCursorPos(&pt);
#else
  float fX, fY;
  SDL_GetGlobalMouseState(&fX, &fY);
  pt.x = (LONG)floorf(fX);
  pt.y = (LONG)floorf(fY);
#endif
  return pt;
}

static void PostMsg(UINT message, WPARAM wParam, LPARAM lParam)
{
  MSG msg;
  msg.hwnd = _pwndMain != NULL ? WindowHandle(_pwndMain) : NULL;
  msg.message = message;
  msg.wParam = wParam;
  msg.lParam = lParam;
  msg.time = (DWORD)SDL_GetTicks();
  msg.pt = ScreenCursorPos();
  _dqMessages.push_back(msg);
}

// MK_* flags for mouse messages
static WPARAM MouseKeyFlags(void)
{
  const SDL_MouseButtonFlags ulButtons = SDL_GetMouseState(NULL, NULL);
  const SDL_Keymod mod = SDL_GetModState();
  WPARAM wFlags = 0;
  if (ulButtons & SDL_BUTTON_LMASK) wFlags |= MK_LBUTTON;
  if (ulButtons & SDL_BUTTON_RMASK) wFlags |= MK_RBUTTON;
  if (ulButtons & SDL_BUTTON_MMASK) wFlags |= MK_MBUTTON;
  if (ulButtons & SDL_BUTTON_X1MASK) wFlags |= MK_XBUTTON1;
  if (ulButtons & SDL_BUTTON_X2MASK) wFlags |= MK_XBUTTON2;
  if (mod & SDL_KMOD_SHIFT) wFlags |= MK_SHIFT;
  if (mod & SDL_KMOD_CTRL) wFlags |= MK_CONTROL;
  return wFlags;
}

static LPARAM ClientPointParam(float fX, float fY)
{
  return MAKELPARAM((SWORD)floorf(fX), (SWORD)floorf(fY));
}

// The Windows virtual key of a key event. Letters follow the keyboard layout when it has Latin
// letters (as VK_A..VK_Z do), the rest follows the key's position on a US keyboard.
static UINT VirtualKey(const SDL_KeyboardEvent &ke)
{
  if (ke.key >= SDLK_A && ke.key <= SDLK_Z) {
    return 'A' + (UINT)(ke.key - SDLK_A);
  }
  const SDL_Scancode sc = ke.scancode;
  if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return 'A' + (sc - SDL_SCANCODE_A);
  if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return '1' + (sc - SDL_SCANCODE_1);
  if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return VK_F1 + (sc - SDL_SCANCODE_F1);
  // the numeric keypad gives navigation keys when Num Lock is off
  const BOOL bNumLock = (ke.mod & SDL_KMOD_NUM) != 0;
  switch (sc) {
  case SDL_SCANCODE_0:            return '0';
  case SDL_SCANCODE_RETURN:       return VK_RETURN;
  case SDL_SCANCODE_ESCAPE:       return VK_ESCAPE;
  case SDL_SCANCODE_BACKSPACE:    return VK_BACK;
  case SDL_SCANCODE_TAB:          return VK_TAB;
  case SDL_SCANCODE_SPACE:        return VK_SPACE;
  case SDL_SCANCODE_MINUS:        return VK_OEM_MINUS;
  case SDL_SCANCODE_EQUALS:       return VK_OEM_PLUS;
  case SDL_SCANCODE_LEFTBRACKET:  return VK_OEM_4;
  case SDL_SCANCODE_RIGHTBRACKET: return VK_OEM_6;
  case SDL_SCANCODE_BACKSLASH:    return VK_OEM_5;
  case SDL_SCANCODE_NONUSHASH:    return VK_OEM_5;
  case SDL_SCANCODE_SEMICOLON:    return VK_OEM_1;
  case SDL_SCANCODE_APOSTROPHE:   return VK_OEM_7;
  case SDL_SCANCODE_GRAVE:        return VK_OEM_3;
  case SDL_SCANCODE_COMMA:        return VK_OEM_COMMA;
  case SDL_SCANCODE_PERIOD:       return VK_OEM_PERIOD;
  case SDL_SCANCODE_SLASH:        return VK_OEM_2;
  case SDL_SCANCODE_NONUSBACKSLASH: return VK_OEM_102;
  case SDL_SCANCODE_CAPSLOCK:     return VK_CAPITAL;
  case SDL_SCANCODE_PRINTSCREEN:  return VK_SNAPSHOT;
  case SDL_SCANCODE_SCROLLLOCK:   return VK_SCROLL;
  case SDL_SCANCODE_PAUSE:        return VK_PAUSE;
  case SDL_SCANCODE_INSERT:       return VK_INSERT;
  case SDL_SCANCODE_HOME:         return VK_HOME;
  case SDL_SCANCODE_PAGEUP:       return VK_PRIOR;
  case SDL_SCANCODE_DELETE:       return VK_DELETE;
  case SDL_SCANCODE_END:          return VK_END;
  case SDL_SCANCODE_PAGEDOWN:     return VK_NEXT;
  case SDL_SCANCODE_RIGHT:        return VK_RIGHT;
  case SDL_SCANCODE_LEFT:         return VK_LEFT;
  case SDL_SCANCODE_DOWN:         return VK_DOWN;
  case SDL_SCANCODE_UP:           return VK_UP;
  case SDL_SCANCODE_NUMLOCKCLEAR: return VK_NUMLOCK;
  case SDL_SCANCODE_KP_DIVIDE:    return VK_DIVIDE;
  case SDL_SCANCODE_KP_MULTIPLY:  return VK_MULTIPLY;
  case SDL_SCANCODE_KP_MINUS:     return VK_SUBTRACT;
  case SDL_SCANCODE_KP_PLUS:      return VK_ADD;
  case SDL_SCANCODE_KP_ENTER:     return VK_RETURN;
  case SDL_SCANCODE_KP_1:         return bNumLock ? VK_NUMPAD1 : VK_END;
  case SDL_SCANCODE_KP_2:         return bNumLock ? VK_NUMPAD2 : VK_DOWN;
  case SDL_SCANCODE_KP_3:         return bNumLock ? VK_NUMPAD3 : VK_NEXT;
  case SDL_SCANCODE_KP_4:         return bNumLock ? VK_NUMPAD4 : VK_LEFT;
  case SDL_SCANCODE_KP_5:         return bNumLock ? VK_NUMPAD5 : VK_CLEAR;
  case SDL_SCANCODE_KP_6:         return bNumLock ? VK_NUMPAD6 : VK_RIGHT;
  case SDL_SCANCODE_KP_7:         return bNumLock ? VK_NUMPAD7 : VK_HOME;
  case SDL_SCANCODE_KP_8:         return bNumLock ? VK_NUMPAD8 : VK_UP;
  case SDL_SCANCODE_KP_9:         return bNumLock ? VK_NUMPAD9 : VK_PRIOR;
  case SDL_SCANCODE_KP_0:         return bNumLock ? VK_NUMPAD0 : VK_INSERT;
  case SDL_SCANCODE_KP_PERIOD:    return bNumLock ? VK_DECIMAL : VK_DELETE;
  case SDL_SCANCODE_APPLICATION:  return VK_APPS;
  // messages carry the generic modifier keys (GetAsyncKeyState knows left and right)
  case SDL_SCANCODE_LCTRL:
  case SDL_SCANCODE_RCTRL:        return VK_CONTROL;
  case SDL_SCANCODE_LSHIFT:
  case SDL_SCANCODE_RSHIFT:       return VK_SHIFT;
  case SDL_SCANCODE_LALT:
  case SDL_SCANCODE_RALT:         return VK_MENU;
  case SDL_SCANCODE_LGUI:         return VK_LWIN;
  case SDL_SCANCODE_RGUI:         return VK_RWIN;
  case SDL_SCANCODE_LANG1:        return VK_HANGUL;
  case SDL_SCANCODE_LANG2:        return VK_HANJA;
  default:                        return 0;
  }
}

// keys whose messages have the extended-key bit (24) set on Windows
static BOOL IsExtendedKey(SDL_Scancode sc)
{
  switch (sc) {
  case SDL_SCANCODE_RCTRL: case SDL_SCANCODE_RALT:
  case SDL_SCANCODE_INSERT: case SDL_SCANCODE_DELETE: case SDL_SCANCODE_HOME: case SDL_SCANCODE_END:
  case SDL_SCANCODE_PAGEUP: case SDL_SCANCODE_PAGEDOWN:
  case SDL_SCANCODE_LEFT: case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_UP: case SDL_SCANCODE_DOWN:
  case SDL_SCANCODE_KP_DIVIDE: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_NUMLOCKCLEAR:
  case SDL_SCANCODE_LGUI: case SDL_SCANCODE_RGUI: case SDL_SCANCODE_APPLICATION:
    return TRUE;
  default:
    return FALSE;
  }
}

static void TranslateKey(const SDL_KeyboardEvent &ke)
{
  const UINT uVK = VirtualKey(ke);
  if (uVK == 0) {
    return;
  }
  const BOOL bAlt = (ke.mod & SDL_KMOD_ALT) != 0;
  const BOOL bCtrl = (ke.mod & SDL_KMOD_CTRL) != 0;
  // lParam: repeat count, extended key, context (Alt), previous state, transition
  // (the hardware scan code, bits 16-23, is left out)
  LPARAM lParam = 1;
  if (IsExtendedKey(ke.scancode)) lParam |= 1L << 24;
  if (bAlt) lParam |= 1L << 29;

  if (ke.down) {
    if (ke.repeat) lParam |= 1L << 30;
    // Alt combinations and F10 are system keys
    const UINT uMessage = ((bAlt && !bCtrl) || uVK == VK_F10) ? WM_SYSKEYDOWN : WM_KEYDOWN;
    PostMsg(uMessage, uVK, lParam);
    // what TranslateMessage adds for keys that type control characters; printable text comes
    // with SDL_EVENT_TEXT_INPUT
    if (uMessage == WM_KEYDOWN) {
      if (uVK == VK_BACK || uVK == VK_TAB || uVK == VK_RETURN || uVK == VK_ESCAPE) {
        PostMsg(WM_CHAR, uVK, lParam);
      } else if (bCtrl && uVK >= 'A' && uVK <= 'Z') {
        PostMsg(WM_CHAR, uVK - 'A' + 1, lParam);  // Ctrl+A..Z: 1..26
      }
    }
  } else {
    lParam |= 3L << 30;
    const UINT uMessage = (bAlt || uVK == VK_MENU || uVK == VK_F10) ? WM_SYSKEYUP : WM_KEYUP;
    PostMsg(uMessage, uVK, lParam);
  }
}

// typed text: one WM_CHAR per byte in the client's code page (as an ANSI window gets it)
static void TranslateText(const char *strUTF8)
{
  StackMultiByteToWideChar(CP_UTF8, strUTF8, -1, wcsText);
  if (wcsText == NULL) {
    return;
  }
  StackWideCharToMultiByte(CP_ACP, wcsText, -1, strText);
  if (strText == NULL) {
    return;
  }
  for (const char *pch = strText; *pch != 0; pch++) {
    PostMsg(WM_CHAR, (UBYTE)*pch, 1);
  }
}

static void TranslateMouseButton(const SDL_MouseButtonEvent &mbe)
{
  UINT uDown, uUp, uDoubleClick;
  switch (mbe.button) {
  case SDL_BUTTON_LEFT:   uDown = WM_LBUTTONDOWN; uUp = WM_LBUTTONUP; uDoubleClick = WM_LBUTTONDBLCLK; break;
  case SDL_BUTTON_RIGHT:  uDown = WM_RBUTTONDOWN; uUp = WM_RBUTTONUP; uDoubleClick = WM_RBUTTONDBLCLK; break;
  case SDL_BUTTON_MIDDLE: uDown = WM_MBUTTONDOWN; uUp = WM_MBUTTONUP; uDoubleClick = WM_MBUTTONDBLCLK; break;
  default: return;
  }
  const LPARAM lParam = ClientPointParam(mbe.x, mbe.y);
  if (mbe.down) {
    // every second click of a series is a double click, as with CS_DBLCLKS
    PostMsg((mbe.clicks % 2 == 0) ? uDoubleClick : uDown, MouseKeyFlags(), lParam);
  } else {
    PostMsg(uUp, MouseKeyFlags(), lParam);
  }
}

static void TranslateMouseWheel(const SDL_MouseWheelEvent &mwe)
{
  float fNotches = mwe.y;
  if (mwe.direction == SDL_MOUSEWHEEL_FLIPPED) fNotches = -fNotches;
  // trackpads scroll by fractions of a notch: report WHEEL_DELTA units as they add up
  _fWheelRemainder += fNotches * WHEEL_DELTA;
  const int iDelta = (int)_fWheelRemainder;
  if (iDelta == 0) {
    return;
  }
  _fWheelRemainder -= iDelta;
  // the wheel message has the cursor in screen coordinates
  const POINT pt = ScreenCursorPos();
  PostMsg(WM_MOUSEWHEEL, MAKEWPARAM(MouseKeyFlags(), (SWORD)iDelta), MAKELPARAM((SWORD)pt.x, (SWORD)pt.y));
}

static void TranslateEvent(const SDL_Event &ev)
{
  switch (ev.type) {
  case SDL_EVENT_QUIT:
  case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    PostMsg(WM_CLOSE, 0, 0);
    break;
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP:
    TranslateKey(ev.key);
    break;
  case SDL_EVENT_TEXT_INPUT:
    TranslateText(ev.text.text);
    break;
  case SDL_EVENT_MOUSE_MOTION:
    PostMsg(WM_MOUSEMOVE, MouseKeyFlags(), ClientPointParam(ev.motion.x, ev.motion.y));
    break;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP:
    TranslateMouseButton(ev.button);
    break;
  case SDL_EVENT_MOUSE_WHEEL:
    TranslateMouseWheel(ev.wheel);
    break;
  case SDL_EVENT_WINDOW_FOCUS_GAINED:
    PostMsg(WM_ACTIVATEAPP, TRUE, 0);
    PostMsg(WM_ACTIVATE, WA_ACTIVE, 0);
    PostMsg(WM_SETFOCUS, 0, 0);
    break;
  case SDL_EVENT_WINDOW_FOCUS_LOST:
    PostMsg(WM_ACTIVATE, WA_INACTIVE, 0);
    PostMsg(WM_KILLFOCUS, 0, 0);
    PostMsg(WM_ACTIVATEAPP, FALSE, 0);
    break;
  case SDL_EVENT_WINDOW_MOVED:
    PostMsg(WM_MOVE, 0, MAKELPARAM((SWORD)ev.window.data1, (SWORD)ev.window.data2));
    break;
  case SDL_EVENT_WINDOW_RESIZED:
    PostMsg(WM_SIZE, 0 /*SIZE_RESTORED*/, MAKELPARAM((WORD)ev.window.data1, (WORD)ev.window.data2));
    break;
  default:
    break;
  }
}

BOOL PLT_PeekMessage(MSG *pmsg)
{
  // translate pending events until there is a message to give
  SDL_Event ev;
  while (_dqMessages.empty() && SDL_PollEvent(&ev)) {
    TranslateEvent(ev);
  }
  if (_dqMessages.empty()) {
    return FALSE;
  }
  *pmsg = _dqMessages.front();
  _dqMessages.pop_front();
  return TRUE;
}

#endif  // LC_PLATFORM_SDL
