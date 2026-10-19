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
    // the context takes the new drawable size (none yet when the window is first set up)
    [[NSOpenGLContext currentContext] update];
  }
  return bHighDensity ? (float)nswnd.backingScaleFactor : 1.0f;
}
