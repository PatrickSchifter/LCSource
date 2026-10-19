#include "StdH.h"

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
#include <vector>
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
  // (no high pixel density yet); outside Windows OpenGL draws on it (Graphics/Gfx_OpenGL.cpp),
  // on Windows Direct3D 8 takes its HWND
#ifdef PLATFORM_WIN32
  const SDL_WindowFlags ulFlags = SDL_WINDOW_HIDDEN;
#else
  const SDL_WindowFlags ulFlags = SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL;
#endif
  _pwndMain = SDL_CreateWindow(strTitle, 640, 480, ulFlags);
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

// the modifiers that count as Control (Command too on macOS)
#ifdef __APPLE__
  #define CONTROL_MODS (SDL_KMOD_CTRL|SDL_KMOD_GUI)
#else
  #define CONTROL_MODS SDL_KMOD_CTRL
#endif

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
  if (mod & CONTROL_MODS) wFlags |= MK_CONTROL;
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
#ifdef __APPLE__
  // Command does what Control does on Windows (copy, paste, the game's Ctrl shortcuts)
  case SDL_SCANCODE_LGUI:
  case SDL_SCANCODE_RGUI:         return VK_CONTROL;
#else
  case SDL_SCANCODE_LGUI:         return VK_LWIN;
  case SDL_SCANCODE_RGUI:         return VK_RWIN;
#endif
  case SDL_SCANCODE_LANG1:        return VK_HANGUL;
  case SDL_SCANCODE_LANG2:        return VK_HANJA;
  default:                        return 0;
  }
}

// The PC (set 1) scan code of a key, as Windows puts it in bits 16-24 of a key message's lParam,
// with 0x100 for the extended (E0) keys. The game reads it for its shortcuts
// (CUIPlayerInfo::ProcessShortCut) and for inp_iKeyboardReadingMethod 2 (Input.cpp).
static UINT PCScanCode(SDL_Scancode sc)
{
  // letters, in SDL (USB) order A..Z
  static const UBYTE aubLetters[26] = {
    0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,  // A..M
    0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C,  // N..Z
  };
  if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return aubLetters[sc - SDL_SCANCODE_A];
  if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0) return 0x02 + (sc - SDL_SCANCODE_1);  // 1..9, 0
  if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F10) return 0x3B + (sc - SDL_SCANCODE_F1);
  switch (sc) {
  case SDL_SCANCODE_RETURN:       return 0x1C;
  case SDL_SCANCODE_ESCAPE:       return 0x01;
  case SDL_SCANCODE_BACKSPACE:    return 0x0E;
  case SDL_SCANCODE_TAB:          return 0x0F;
  case SDL_SCANCODE_SPACE:        return 0x39;
  case SDL_SCANCODE_MINUS:        return 0x0C;
  case SDL_SCANCODE_EQUALS:       return 0x0D;
  case SDL_SCANCODE_LEFTBRACKET:  return 0x1A;
  case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
  case SDL_SCANCODE_BACKSLASH:
  case SDL_SCANCODE_NONUSHASH:    return 0x2B;
  case SDL_SCANCODE_SEMICOLON:    return 0x27;
  case SDL_SCANCODE_APOSTROPHE:   return 0x28;
  case SDL_SCANCODE_GRAVE:        return 0x29;
  case SDL_SCANCODE_COMMA:        return 0x33;
  case SDL_SCANCODE_PERIOD:       return 0x34;
  case SDL_SCANCODE_SLASH:        return 0x35;
  case SDL_SCANCODE_NONUSBACKSLASH: return 0x56;
  case SDL_SCANCODE_CAPSLOCK:     return 0x3A;
  case SDL_SCANCODE_F11:          return 0x57;
  case SDL_SCANCODE_F12:          return 0x58;
  case SDL_SCANCODE_PRINTSCREEN:  return 0x137;
  case SDL_SCANCODE_SCROLLLOCK:   return 0x46;
  case SDL_SCANCODE_PAUSE:        return 0x45;
  case SDL_SCANCODE_INSERT:       return 0x152;
  case SDL_SCANCODE_HOME:         return 0x147;
  case SDL_SCANCODE_PAGEUP:       return 0x149;
  case SDL_SCANCODE_DELETE:       return 0x153;
  case SDL_SCANCODE_END:          return 0x14F;
  case SDL_SCANCODE_PAGEDOWN:     return 0x151;
  case SDL_SCANCODE_RIGHT:        return 0x14D;
  case SDL_SCANCODE_LEFT:         return 0x14B;
  case SDL_SCANCODE_DOWN:         return 0x150;
  case SDL_SCANCODE_UP:           return 0x148;
  case SDL_SCANCODE_NUMLOCKCLEAR: return 0x145;
  case SDL_SCANCODE_KP_DIVIDE:    return 0x135;
  case SDL_SCANCODE_KP_MULTIPLY:  return 0x37;
  case SDL_SCANCODE_KP_MINUS:     return 0x4A;
  case SDL_SCANCODE_KP_PLUS:      return 0x4E;
  case SDL_SCANCODE_KP_ENTER:     return 0x11C;
  case SDL_SCANCODE_KP_1:         return 0x4F;
  case SDL_SCANCODE_KP_2:         return 0x50;
  case SDL_SCANCODE_KP_3:         return 0x51;
  case SDL_SCANCODE_KP_4:         return 0x4B;
  case SDL_SCANCODE_KP_5:         return 0x4C;
  case SDL_SCANCODE_KP_6:         return 0x4D;
  case SDL_SCANCODE_KP_7:         return 0x47;
  case SDL_SCANCODE_KP_8:         return 0x48;
  case SDL_SCANCODE_KP_9:         return 0x49;
  case SDL_SCANCODE_KP_0:         return 0x52;
  case SDL_SCANCODE_KP_PERIOD:    return 0x53;
  case SDL_SCANCODE_APPLICATION:  return 0x15D;
  case SDL_SCANCODE_LCTRL:        return 0x1D;
  case SDL_SCANCODE_LSHIFT:       return 0x2A;
  case SDL_SCANCODE_LALT:         return 0x38;
  case SDL_SCANCODE_LGUI:         return 0x15B;
  case SDL_SCANCODE_RCTRL:        return 0x11D;
  case SDL_SCANCODE_RSHIFT:       return 0x36;
  case SDL_SCANCODE_RALT:         return 0x138;
  case SDL_SCANCODE_RGUI:         return 0x15C;
  default:                        return 0;
  }
}

static void TranslateKey(const SDL_KeyboardEvent &ke)
{
  const UINT uVK = VirtualKey(ke);
  if (uVK == 0) {
    return;
  }
  const BOOL bAlt = (ke.mod & SDL_KMOD_ALT) != 0;
  const BOOL bCtrl = (ke.mod & CONTROL_MODS) != 0;
  // lParam: repeat count, scan code and extended key (bits 16-24), context (Alt), previous
  // state, transition
  LPARAM lParam = 1 | ((LPARAM)PCScanCode(ke.scancode) << 16);
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

// Input.cpp: keys and mouse buttons for the game controls (what its Windows message hooks see)
extern void INP_HandleMessage(const MSG &msg);

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
  INP_HandleMessage(*pmsg);
  return TRUE;
}

BOOL PLT_IsVirtualKeyDown(UINT uVirtKey)
{
  // mouse buttons
  const SDL_MouseButtonFlags ulButtons = SDL_GetMouseState(NULL, NULL);
  switch (uVirtKey) {
  case VK_LBUTTON:  return (ulButtons & SDL_BUTTON_LMASK) != 0;
  case VK_RBUTTON:  return (ulButtons & SDL_BUTTON_RMASK) != 0;
  case VK_MBUTTON:  return (ulButtons & SDL_BUTTON_MMASK) != 0;
  case VK_XBUTTON1: return (ulButtons & SDL_BUTTON_X1MASK) != 0;
  case VK_XBUTTON2: return (ulButtons & SDL_BUTTON_X2MASK) != 0;
  default: break;
  }
  int ctKeys = 0;
  const bool *abState = SDL_GetKeyboardState(&ctKeys);
  // modifiers, generic and left/right
  switch (uVirtKey) {
  case VK_SHIFT:    return abState[SDL_SCANCODE_LSHIFT] || abState[SDL_SCANCODE_RSHIFT];
#ifdef __APPLE__
  case VK_CONTROL:  return abState[SDL_SCANCODE_LCTRL] || abState[SDL_SCANCODE_RCTRL]
                        || abState[SDL_SCANCODE_LGUI] || abState[SDL_SCANCODE_RGUI];
  case VK_LCONTROL: return abState[SDL_SCANCODE_LCTRL] || abState[SDL_SCANCODE_LGUI];
#else
  case VK_CONTROL:  return abState[SDL_SCANCODE_LCTRL] || abState[SDL_SCANCODE_RCTRL];
  case VK_LCONTROL: return abState[SDL_SCANCODE_LCTRL];
#endif
  case VK_MENU:     return abState[SDL_SCANCODE_LALT] || abState[SDL_SCANCODE_RALT];
  case VK_LSHIFT:   return abState[SDL_SCANCODE_LSHIFT];
  case VK_RSHIFT:   return abState[SDL_SCANCODE_RSHIFT];
  case VK_RCONTROL: return abState[SDL_SCANCODE_RCTRL];
  case VK_LMENU:    return abState[SDL_SCANCODE_LALT];
  case VK_RMENU:    return abState[SDL_SCANCODE_RALT];
  default: break;
  }
  // any other key: the pressed keys that translate to this virtual key
  SDL_KeyboardEvent ke;
  memset(&ke, 0, sizeof(ke));
  ke.mod = SDL_GetModState();
  for (int iScan = 0; iScan < ctKeys; iScan++) {
    if (!abState[iScan]) continue;
    ke.scancode = (SDL_Scancode)iScan;
    ke.key = SDL_GetKeyFromScancode(ke.scancode, SDL_KMOD_NONE, false);
    if (VirtualKey(ke) == uVirtKey) {
      return TRUE;
    }
  }
  return FALSE;
}

/////////////////////////////////////////////////////////////////////
// cursors

static UWORD ReadUWORD(const UBYTE *pub) { return (UWORD)(pub[0] | (pub[1] << 8)); }
static ULONG ReadULONG(const UBYTE *pub) { return (ULONG)(pub[0] | (pub[1] << 8) | (pub[2] << 16) | ((ULONG)pub[3] << 24)); }

// The first image of a .cur file (BMP, 1/4/8/24/32 bits, with its AND mask) as a color cursor.
static SDL_Cursor *CursorFromCur(const UBYTE *pub, size_t ctSize)
{
  // header: reserved, type 2 (cursor), count; first entry: width, height, colors, reserved,
  // hot spot x and y, image size and offset
  if (ctSize < 22 || ReadUWORD(pub + 2) != 2 || ReadUWORD(pub + 4) < 1) return NULL;
  const int iHotX = ReadUWORD(pub + 10);
  const int iHotY = ReadUWORD(pub + 12);
  const ULONG ulOffset = ReadULONG(pub + 18);
  if (ulOffset + 40 > ctSize) return NULL;
  const UBYTE *pubBMP = pub + ulOffset;
  if (ReadULONG(pubBMP) < 40) return NULL;  // PNG cursors are not used by the game
  const int iWidth = (int)ReadULONG(pubBMP + 4);
  const int iHeight = (int)ReadULONG(pubBMP + 8) / 2;  // color image and mask
  const int iBits = ReadUWORD(pubBMP + 14);
  ULONG ctColors = ReadULONG(pubBMP + 32);
  if (iBits <= 8 && ctColors == 0) ctColors = 1U << iBits;
  if (iBits > 8) ctColors = 0;
  const UBYTE *pubPalette = pubBMP + ReadULONG(pubBMP);
  const UBYTE *pubXOR = pubPalette + ctColors * 4;
  const int ctXORStride = ((iWidth * iBits + 31) / 32) * 4;
  const UBYTE *pubAND = pubXOR + ctXORStride * iHeight;
  const int ctANDStride = ((iWidth + 31) / 32) * 4;
  if (iWidth <= 0 || iHeight <= 0 || pubAND + ctANDStride * iHeight > pub + ctSize) return NULL;

  SDL_Surface *psf = SDL_CreateSurface(iWidth, iHeight, SDL_PIXELFORMAT_ARGB8888);
  if (psf == NULL) return NULL;
  for (int y = 0; y < iHeight; y++) {
    // rows are stored bottom-up
    const UBYTE *pubRow = pubXOR + (iHeight - 1 - y) * ctXORStride;
    const UBYTE *pubMask = pubAND + (iHeight - 1 - y) * ctANDStride;
    ULONG *pulOut = (ULONG*)((UBYTE*)psf->pixels + y * psf->pitch);
    for (int x = 0; x < iWidth; x++) {
      ULONG ulB, ulG, ulR, ulA = 255;
      if (iBits <= 8) {
        const int iBit = x * iBits;
        const int iIndex = (pubRow[iBit / 8] >> (8 - iBits - (iBit % 8))) & ((1 << iBits) - 1);
        const UBYTE *pubColor = pubPalette + iIndex * 4;
        ulB = pubColor[0]; ulG = pubColor[1]; ulR = pubColor[2];
      } else {
        const UBYTE *pubColor = pubRow + x * (iBits / 8);
        ulB = pubColor[0]; ulG = pubColor[1]; ulR = pubColor[2];
        if (iBits == 32) ulA = pubColor[3];
      }
      // the AND mask makes a pixel transparent (32-bit images carry alpha instead)
      if (iBits != 32 && ((pubMask[x / 8] >> (7 - x % 8)) & 1)) ulA = 0;
      pulOut[x] = (ulA << 24) | (ulR << 16) | (ulG << 8) | ulB;
    }
  }
  SDL_Cursor *pcur = SDL_CreateColorCursor(psf, iHotX, iHotY);
  SDL_DestroySurface(psf);
  return pcur;
}

HCURSOR PLT_LoadCursorFile(const char *strFileName)
{
  FILE *pFile = fopen(strFileName, "rb");
  if (pFile == NULL) {
    CPrintF("Cannot open cursor '%s'\n", strFileName);
    return NULL;
  }
  std::vector<UBYTE> aub;
  UBYTE aubChunk[4096];
  size_t ctRead;
  while ((ctRead = fread(aubChunk, 1, sizeof(aubChunk), pFile)) > 0) {
    aub.insert(aub.end(), aubChunk, aubChunk + ctRead);
  }
  fclose(pFile);
  SDL_Cursor *pcur = aub.empty() ? NULL : CursorFromCur(aub.data(), aub.size());
  if (pcur == NULL) {
    CPrintF("Cannot read cursor '%s'\n", strFileName);
  }
  return (HCURSOR)pcur;
}

void PLT_SetCursor(HCURSOR hCursor)
{
  SDL_SetCursor(hCursor != NULL ? (SDL_Cursor*)hCursor : SDL_GetDefaultCursor());
}

void PLT_SetTextInputArea(PIX pixX, PIX pixY, PIX pixWidth, PIX pixHeight)
{
  if (_pwndMain == NULL) return;
  SDL_Rect rect;
  rect.x = pixX;
  rect.y = pixY;
  rect.w = pixWidth;
  rect.h = pixHeight;
  SDL_SetTextInputArea(_pwndMain, &rect, 0);
}

BOOL PLT_GetClipboardText(char *strBuffer, INDEX ctBufferSize)
{
  if (ctBufferSize <= 0) return FALSE;
  strBuffer[0] = 0;
  char *strUTF8 = SDL_GetClipboardText();
  if (strUTF8 == NULL || strUTF8[0] == 0) {
    SDL_free(strUTF8);
    return FALSE;
  }
  StackMultiByteToWideChar(CP_UTF8, strUTF8, -1, wcsText);
  SDL_free(strUTF8);
  if (wcsText == NULL) return FALSE;
  StackWideCharToMultiByte(CP_ACP, wcsText, -1, strText);
  if (strText == NULL) return FALSE;
  strncpy(strBuffer, strText, ctBufferSize - 1);
  strBuffer[ctBufferSize - 1] = 0;
  return TRUE;
}

BOOL PLT_SetClipboardText(const char *strText)
{
  StackMultiByteToWideChar(CP_ACP, strText, -1, wcsText);
  if (wcsText == NULL) return FALSE;
  StackWideCharToMultiByte(CP_UTF8, wcsText, -1, strUTF8);
  if (strUTF8 == NULL) return FALSE;
  return SDL_SetClipboardText(strUTF8);
}

#endif  // LC_PLATFORM_SDL
