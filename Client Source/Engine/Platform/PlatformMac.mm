// macOS trackpad gestures for the SDL platform layer (PlatformSDL.cpp), which SDL does not tell
// apart from a mouse: a two-finger drag turns the camera like a right-button drag, and a pinch
// zooms like the mouse wheel. Scrolling from a mouse wheel (no precise deltas) is left to SDL.

#import <AppKit/AppKit.h>

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
