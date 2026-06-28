#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class iTVPTexture2D;
class iTVPLayerManager;
class tTVPBaseTexture;
class tTJSNI_BaseWindow;
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

struct TVPSDLLoadingConsoleLineSnapshot {
    std::string message;
    bool important = false;
};

struct TVPSDLLoadingConsoleSnapshot {
    bool active = false;
    uint64_t session = 0;
    uint64_t totalLines = 0;
    std::vector<TVPSDLLoadingConsoleLineSnapshot> lines;
};

struct TVPSDLRenderOverlaySnapshot {
    bool showFps = false;
    bool available = false;
    double fps = 0.0;
    unsigned int drawCount = 0;
    uint64_t videoMemoryBytes = 0;
    int selfMemoryMb = 0;
    int freeMemoryMb = 0;
    uint64_t presentedFrames = 0;
    uint64_t sequence = 0;
    std::string rendererName;
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
void TVPSDLQueueFlutterTouchBegin(int id, float x, float y);
void TVPSDLQueueFlutterTouchEnd(int id, float x, float y);
void TVPSDLQueueFlutterTouchMove(int count, const int *ids, const float *xs,
                                 const float *ys);
void TVPSDLQueueFlutterTouchCancel(int count, const int *ids, const float *xs,
                                   const float *ys);
bool TVPSDLDispatchCharInput(int keyCode);
bool TVPSDLDispatchTextInput(const char *text);
bool TVPSDLDispatchDeleteBackward();
bool TVPSDLDispatchAndroidKeyAction(int keyCode, bool isPress);
bool TVPSDLDispatchAndroidHoverMove(float x, float y);
bool TVPSDLDispatchAndroidMouseScroll(float x, float y, float scroll);
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
void TVPSDLRecordLoadingConsoleShow(const char *path, int frameWidth,
                                    int frameHeight, int sceneWidth,
                                    int sceneHeight, float scale);
void TVPSDLRecordLoadingConsoleLine(const char *message, bool important);
void TVPSDLRecordLoadingConsoleHide(const char *reason);
TVPSDLLoadingConsoleSnapshot TVPSDLGetLoadingConsoleSnapshot();
void TVPSDLRecordRenderOverlayFrame(float deltaSeconds);
TVPSDLRenderOverlaySnapshot TVPSDLGetRenderOverlaySnapshot();
void TVPSDLSetScreenTakeoverEnabled(bool enabled, const char *reason,
                                    int frameWidth, int frameHeight,
                                    int sceneWidth, int sceneHeight);
bool TVPSDLIsScreenTakeoverSupported();
bool TVPSDLIsScreenTakeoverEnabled();
bool TVPSDLHasScreenPresenterPresented();
extern "C" void TVPSDLGetPresentedSurfaceSize(int *width, int *height);
#if defined(__ANDROID__)
extern "C" void
TVPSDLNotifyAndroidFlutterGameSurfaceChanged(const char *reason);
#endif
bool TVPSDLIsRenderDiagnosticsEnabled();
bool TVPSDLTryPresentTexture(iTVPTexture2D *texture, const char *stage,
                             int layerWidth, int layerHeight);
bool TVPSDLPresentHostWindowTexture(tTJSNI_BaseWindow *window,
                                    iTVPTexture2D *texture, const char *stage,
                                    int layerWidth, int layerHeight);
bool TVPSDLPumpScreenPresenter(const char *stage);

TVPSDLGameLaunchResult
TVPSDLRunGameLaunch(const TVPSDLGameLaunchCallbacks &callbacks);
