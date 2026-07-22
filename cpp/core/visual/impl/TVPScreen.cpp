#include "tjsCommHead.h"

#include "TVPScreen.h"
#include "Application.h"
#include "sdl/SDLPresentTypes.h"

int tTVPScreen::GetWidth() {
    return kTVPSDLFixedGameSurfaceWidth;
}
int tTVPScreen::GetHeight() {
    return kTVPSDLFixedGameSurfaceHeight;
}

int tTVPScreen::GetDesktopLeft() { return 0; }
int tTVPScreen::GetDesktopTop() { return 0; }
int tTVPScreen::GetDesktopWidth() { return GetWidth(); }
int tTVPScreen::GetDesktopHeight() { return GetHeight(); }
