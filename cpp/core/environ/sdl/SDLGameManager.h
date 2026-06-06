#pragma once

#include <functional>
#include <string>

class iTVPTexture2D;
class iTVPLayerManager;
class tTVPBaseTexture;
struct tTVPRect;

struct TVPSDLRuntimeInfo {
    std::string compiledVersion;
    std::string linkedVersion;
    std::string revision;
    std::string platform;
    std::string videoDriver;
    std::string audioDriver;
    bool eventsReady = false;
    bool videoReady = false;
    bool audioReady = false;
};

struct TVPSDLGameLaunchCallbacks {
    std::function<void()> initializePreferences;
    std::function<bool(const std::string &path, const std::string &gameDir)>
        startupFrom;
    std::function<void()> showFileSelector;
    std::function<void(const std::string &message)> log;
};

enum class TVPSDLGameLaunchResult {
    Started,
    StartupArgHandled,
    FileSelectorShown,
    NoFallback,
};

bool TVPSDLInitializeRuntime();
TVPSDLRuntimeInfo TVPSDLGetRuntimeInfo();
void TVPSDLRecordAndroidLifecycle(const char *eventName, const char *detail);
void TVPSDLRecordAndroidInput(const char *eventName, int itemCount,
                              float x = 0.0f, float y = 0.0f, int code = 0,
                              bool state = false);
void TVPSDLProcessAndroidInputQueue();
void TVPSDLRecordRenderFrame(int layerWidth, int layerHeight,
                             int internalWidth, int internalHeight,
                             bool textureChanged, const void *sourceTexture,
                             const void *currentTexture,
                             const void *newTexture);
void TVPSDLRecordPresenterFrame(iTVPTexture2D *texture, const char *stage,
                                int layerWidth, int layerHeight);
void TVPSDLRecordBitmapCompletionStart(iTVPLayerManager *manager,
                                       int sourceWidth, int sourceHeight,
                                       int destWidth, int destHeight);
void TVPSDLRecordBitmapCompletionRegion(iTVPLayerManager *manager, int x,
                                        int y, tTVPBaseTexture *bitmap,
                                        const tTVPRect &clipRect, int type,
                                        int opacity, int sourceWidth,
                                        int sourceHeight);
void TVPSDLRecordBitmapCompletionEnd(iTVPLayerManager *manager,
                                     int sourceWidth, int sourceHeight);

TVPSDLGameLaunchResult
TVPSDLRunGameLaunch(const TVPSDLGameLaunchCallbacks &callbacks);
