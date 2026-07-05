#include "tjsCommHead.h"

#include "TVPScreen.h"
#include "Application.h"
#include "sdl/SDLPresentTypes.h"

#ifndef KRKR2_ENABLE_COCOS_HOST
#define KRKR2_ENABLE_COCOS_HOST 0
#endif

#if KRKR2_ENABLE_COCOS_HOST
#include "cocos2d.h"
#endif

int tTVPScreen::GetWidth() {
#if defined(__ANDROID__) && !KRKR2_ENABLE_COCOS_HOST
    return kTVPSDLFixedGameSurfaceWidth;
#else
    return 2048;
#endif
}
int tTVPScreen::GetHeight() {
#if defined(__ANDROID__) && !KRKR2_ENABLE_COCOS_HOST
    return kTVPSDLFixedGameSurfaceHeight;
#elif KRKR2_ENABLE_COCOS_HOST
    const cocos2d::Size &size =
        cocos2d::Director::getInstance()->getOpenGLView()->getFrameSize();
    int w = GetWidth();
    int h = w * (size.height / size.width);
    (void)h;
    return w;
#else
    return 1080;
#endif
}

int tTVPScreen::GetDesktopLeft() { return 0; }
int tTVPScreen::GetDesktopTop() { return 0; }
int tTVPScreen::GetDesktopWidth() { return GetWidth(); }
int tTVPScreen::GetDesktopHeight() { return GetHeight(); }
