#pragma once

#include "RenderManager.h"

#include <SDL3/SDL.h>

class iTVPTexture2D;

bool TVPSDLAndroidFlutterPresenterGetPresentedSurfaceSize(int *width,
                                                          int *height);
bool TVPSDLAndroidFlutterPresenterHasPresentedSurface();
void TVPSDLAndroidFlutterPresenterRememberPresentedSurfaceSize(int width,
                                                               int height);
void TVPSDLAndroidFlutterPresenterMarkSurfaceChanged();
bool TVPSDLAndroidFlutterPresenterConsumeForceFullFramePresent();
bool TVPSDLAndroidFlutterPresenterIsDirectPartialPresentEnabled();
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
