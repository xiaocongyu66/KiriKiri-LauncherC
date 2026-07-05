#pragma once

#include "RenderManager.h"
#include "SDLPresentTypes.h"

#include <SDL3/SDL.h>

#include <iosfwd>

class iTVPTexture2D;

bool TVPSDLAndroidFlutterPresenterGetPresentedSurfaceSize(int *width,
                                                          int *height);
bool TVPSDLAndroidFlutterPresenterHasPresentedSurface();
void TVPSDLAndroidFlutterPresenterRememberPresentedSurfaceSize(int width,
                                                               int height);
void TVPSDLAndroidFlutterPresenterMarkSurfaceChanged();
bool TVPSDLAndroidFlutterPresenterConsumeForceFullFramePresent();
bool TVPSDLAndroidFlutterPresenterIsDirectPartialPresentEnabled();
const char *TVPSDLAndroidFlutterPresenterPresentPathLogName(
    TVPSDLPresentPath path);
bool TVPSDLAndroidFlutterPresenterTryPresentTexturePlan(
    iTVPTexture2D *texture, TVPTextureFormat::e format, const char *stage,
    const TVPSDLTexturePresentPlan &plan,
    TVPSDLTexturePresentResult &result);
void TVPSDLAndroidFlutterPresenterAppendEGLOverlayInfo(
    std::ostream &rendererInfo);
bool TVPSDLAndroidFlutterPresenterIsEGLHighPerformanceActive();
bool TVPSDLAndroidFlutterPresenterIsEGLCpuCopyFreeActive();
void TVPSDLAndroidFlutterPresenterNotifySurfaceChanged(const char *reason);
bool TVPSDLAndroidFlutterPresenterTryPresentTexture(iTVPTexture2D *texture,
                                                    TVPTextureFormat::e format,
                                                    int surfaceWidth,
                                                    int surfaceHeight,
                                                    const SDL_Rect &rect,
                                                    const char *stage);
bool TVPSDLAndroidFlutterPresenterTryPresentSurface(SDL_Surface *surface,
                                                    int surfaceWidth,
                                                    int surfaceHeight,
                                                    int pitch,
                                                    const SDL_Rect &rect,
                                                    const char *stage);

extern "C" bool TVPSDLAndroidSwapExternalPresenterIfDirty();
