#ifndef SE_INCL_PLATFORM_H
#define SE_INCL_PLATFORM_H
#ifdef PRAGMA_ONCE
  #pragma once
#endif

// The SDL3 platform layer (PlatformSDL.cpp): the main window and its events. It is what the
// client uses outside Windows, and on Windows when built with LC_PLATFORM_SDL (to test it in the
// real game). Events come out as the Win32 messages that the game and the UI already handle
// (MSG with WM_*, VK_* and MK_* values); the window handle the engine keeps (_hwndMain) is the
// real HWND on Windows and the SDL window elsewhere.
#ifdef LC_PLATFORM_SDL

struct SDL_Window;

// start and stop SDL (video, events, gamepads)
ENGINE_API BOOL PLT_Init(void);
ENGINE_API void PLT_End(void);

// the main window; created hidden, then shown by PLT_SetMainWindowMode()
ENGINE_API HWND PLT_CreateMainWindow(const char *strTitle);
ENGINE_API void PLT_DestroyMainWindow(void);
ENGINE_API SDL_Window *PLT_GetMainWindow(void);
// client area size, windowed (centered, or at iLeft/iTop when not negative) or fullscreen
ENGINE_API void PLT_SetMainWindowMode(PIX pixWidth, PIX pixHeight, BOOL bFullScreen, INDEX iLeft, INDEX iTop);
ENGINE_API void PLT_SetMainWindowTitle(const char *strTitle);
ENGINE_API BOOL PLT_IsMainWindowMinimized(void);
ENGINE_API void PLT_GetDesktopSize(PIX &pixWidth, PIX &pixHeight);

// the next message translated from the window's events, as PeekMessage(PM_REMOVE) would give it
ENGINE_API BOOL PLT_PeekMessage(MSG *pmsg);

// whether a key or mouse button is down now, by Windows virtual key (GetAsyncKeyState)
ENGINE_API BOOL PLT_IsVirtualKeyDown(UINT uVirtKey);

// mouse cursors from Windows .cur files (the game's own cursors), as HCURSOR; NULL is the
// system arrow
ENGINE_API HCURSOR PLT_LoadCursorFile(const char *strFileName);
ENGINE_API void PLT_SetCursor(HCURSOR hCursor);

#endif  // LC_PLATFORM_SDL

#endif  /* include-once check. */
