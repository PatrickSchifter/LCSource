// macOS pieces of the SDL platform layer (PlatformSDL.cpp). Trackpad gestures, which SDL does
// not tell apart from a mouse: a two-finger drag turns the camera like a right-button drag, and a
// pinch zooms like the mouse wheel. Scrolling from a mouse wheel (no precise deltas) is left to
// SDL. And the window's pixel density (Retina).

#define GL_SILENCE_DEPRECATION  // NSOpenGLContext
#import <AppKit/AppKit.h>
#include <SDL3/SDL.h>

// PlatformSDL.cpp: TRUE when the scroll was taken as camera turning (finger movement in points);
// bGestureStart on the first event of a two-finger drag
extern bool PLT_MacTrackpadScroll(float fDX, float fDY, bool bGestureStart, bool bMomentum);
// PlatformSDL.cpp: pinch, as the change of scale (positive when the fingers spread)
extern void PLT_MacMagnify(float fMagnification);

void PLT_MacInstallGestureMonitors(void)
{
  [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel handler:^NSEvent *(NSEvent *ev) {
    if (!ev.hasPreciseScrollingDeltas) {
      return ev;  // a mouse wheel
    }
    float fDX = (float)ev.scrollingDeltaX;
    float fDY = (float)ev.scrollingDeltaY;
    // the movement of the fingers, whatever the "natural scrolling" setting
    if (!ev.isDirectionInvertedFromDevice) {
      fDX = -fDX;
      fDY = -fDY;
    }
    const bool bGestureStart = (ev.phase & (NSEventPhaseBegan|NSEventPhaseMayBegin)) != 0;
    const bool bMomentum = ev.momentumPhase != NSEventPhaseNone;
    return PLT_MacTrackpadScroll(fDX, fDY, bGestureStart, bMomentum) ? nil : ev;
  }];
  [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskMagnify handler:^NSEvent *(NSEvent *ev) {
    PLT_MacMagnify((float)ev.magnification);
    return ev;
  }];
}

// Retina: OpenGL draws the window at the full pixel density of its display (bHighDensity) or one
// pixel per point, scaled up by the system; gives the drawable's pixels per point. The window is
// created without SDL_WINDOW_HIGH_PIXEL_DENSITY, so SDL's sizes and the game's coordinates stay
// in points (PLT_SetMainWindowMode).
float PLT_MacSetHighDensity(SDL_Window *pwnd, bool bHighDensity)
{
  NSWindow *nswnd = (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(pwnd),
    SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
  if (nswnd == nil) {
    return 1.0f;
  }
  NSView *view = nswnd.contentView;
  if (view.wantsBestResolutionOpenGLSurface != bHighDensity) {
    view.wantsBestResolutionOpenGLSurface = bHighDensity;
  }
  // the context takes the drawable's size now: it may have been created after this was set, or
  // the window may have just gone fullscreen (there is none yet when the window is first set up)
  [[NSOpenGLContext currentContext] update];
  return bHighDensity ? (float)nswnd.backingScaleFactor : 1.0f;
}

// the display the window is on, for CoreGraphics
static CGDirectDisplayID WindowDisplay(SDL_Window *pwnd)
{
  NSWindow *nswnd = (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(pwnd),
    SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
  NSScreen *screen = (nswnd != nil && nswnd.screen != nil) ? nswnd.screen : [NSScreen mainScreen];
  NSNumber *num = screen.deviceDescription[@"NSScreenNumber"];
  return num != nil ? (CGDirectDisplayID)num.unsignedIntValue : CGMainDisplayID();
}

// gamma ramps of the window's display (PLT_SetGammaRamp), as Windows' SetDeviceGammaRamp: 256
// red, then green, then blue values of 0..65535
bool PLT_MacCanSetGammaRamp(SDL_Window *pwnd)
{
  CGGammaValue afR[256], afG[256], afB[256];
  uint32_t ctSamples = 0;
  return CGGetDisplayTransferByTable(WindowDisplay(pwnd), 256, afR, afG, afB, &ctSamples) == kCGErrorSuccess
      && ctSamples > 0;
}

bool PLT_MacSetGammaRamp(SDL_Window *pwnd, const unsigned short *puwRamp)
{
  CGGammaValue afR[256], afG[256], afB[256];
  for (int i = 0; i < 256; i++) {
    afR[i] = puwRamp[i] / 65535.0f;
    afG[i] = puwRamp[i + 256] / 65535.0f;
    afB[i] = puwRamp[i + 512] / 65535.0f;
  }
  return CGSetDisplayTransferByTable(WindowDisplay(pwnd), 256, afR, afG, afB) == kCGErrorSuccess;
}

// the displays' own gamma (ColorSync); the system also restores it when the game quits
void PLT_MacRestoreGammaRamp(void)
{
  CGDisplayRestoreColorSyncSettings();
}

// whether the display of the window (the main display without one) has a camera notch: its full
// panel modes put the top of the picture behind the camera (PlatformSDL.cpp)
bool PLT_MacDisplayHasNotch(SDL_Window *pwnd)
{
  NSScreen *screen = nil;
  if (pwnd != NULL) {
    NSWindow *nswnd = (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(pwnd),
      SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    screen = nswnd.screen;
  }
  if (screen == nil) {
    screen = NSScreen.screens.firstObject;
  }
  if (@available(macOS 12.0, *)) {
    return screen != nil && screen.safeAreaInsets.top > 0;
  }
  return false;
}
