#include "SDLUIManager.h"

#include "NativeLog.h"
#include "SDLGameManager.h"

#include <SDL2/SDL.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct TVPSDLUITemplate {
    std::string name;
    std::string csb;
    std::string csd;
};

struct TVPSDLUIState {
    bool assetsRegistered = false;
    std::string sourceRoot;
    std::string runtimeRoot;
    std::vector<TVPSDLUITemplate> templates;

    bool viewportReady = false;
    int frameWidth = 0;
    int frameHeight = 0;
    int sceneWidth = 0;
    int sceneHeight = 0;
    float scale = 1.0f;

    bool gameMenuCreated = false;
    bool gameMenuShrinked = false;
    bool gameMenuHitted = false;
    bool gameMenuMouseIcon = true;
};

std::mutex gSDLUIMutex;
TVPSDLUIState gSDLUIState;
std::atomic_uint64_t gSDLUIEventSequence{0};
std::atomic_uint64_t gSDLUIMenuActionSequence{0};

std::string SafeString(const char *value) {
    return value ? std::string(value) : std::string();
}

const char *BoolString(bool value) { return value ? "1" : "0"; }

void LogSDLUI(const char *message) {
    TVPNativeLogInfo("sdl-ui", message ? message : "");
}

std::string JoinTemplateNames(const std::vector<TVPSDLUITemplate> &templates) {
    std::string result;
    for(size_t i = 0; i < templates.size(); ++i) {
        if(i)
            result += ",";
        result += templates[i].name;
    }
    return result;
}

std::vector<TVPSDLUITemplate> BuildLegacyTemplates(const std::string &root) {
    const std::string prefix =
        root.empty() || root == "." ? std::string() : root + "/";
    return {
        { "loading-console", "ui/ConsoleWindow.sdlui",
          prefix + "out_ui/ConsoleWindow.sdlui" },
        { "game-menu", prefix + "ui/GameMenu.csb",
          prefix + "out_ui/GameMenu.csd" },
        { "main-file-selector", prefix + "ui/MainFileSelector.csb",
          prefix + "out_ui/MainFileSelector.csd" },
        { "menu-list", prefix + "ui/MenuList.csb",
          prefix + "out_ui/MenuList.csd" },
        { "message-box", prefix + "ui/MessageBox.csb",
          prefix + "out_ui/MessageBox.csd" },
        { "window-manager", prefix + "ui/WinMgrOverlay.csb",
          prefix + "out_ui/WinMgrOverlay.csd" },
    };
}

} // namespace

void TVPSDLUIRegisterLegacyCocosStudioAssets(const char *sourceRoot,
                                             const char *runtimeRoot) {
    TVPSDLInitializeRuntime();
    const std::string source = SafeString(sourceRoot);
    const std::string runtime = SafeString(runtimeRoot);

    std::vector<TVPSDLUITemplate> templates;
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        gSDLUIState.assetsRegistered = true;
        gSDLUIState.sourceRoot = source;
        gSDLUIState.runtimeRoot = runtime;
        gSDLUIState.templates = BuildLegacyTemplates(runtime);
        templates = gSDLUIState.templates;
    }

    const Uint32 initialized = SDL_WasInit(0);
    const std::string templateNames = JoinTemplateNames(templates);
    char message[768];
    std::snprintf(
        message, sizeof(message),
        "assets source=%s runtime=%s templates=%zu names=%s font=%s "
        "events=%d video=%d audio=%d ticks=%u",
        source.c_str(), runtime.c_str(), templates.size(),
        templateNames.c_str(), "NotoSansCJK-Regular.ttc",
        (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        (initialized & SDL_INIT_VIDEO) ? 1 : 0,
        (initialized & SDL_INIT_AUDIO) ? 1 : 0,
        static_cast<unsigned>(SDL_GetTicks()));
    LogSDLUI(message);
}

void TVPSDLUIRecordViewport(int frameWidth, int frameHeight, int sceneWidth,
                            int sceneHeight, float scale) {
    TVPSDLInitializeRuntime();
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        changed = !gSDLUIState.viewportReady ||
            gSDLUIState.frameWidth != frameWidth ||
            gSDLUIState.frameHeight != frameHeight ||
            gSDLUIState.sceneWidth != sceneWidth ||
            gSDLUIState.sceneHeight != sceneHeight ||
            gSDLUIState.scale != scale;
        gSDLUIState.viewportReady = true;
        gSDLUIState.frameWidth = frameWidth;
        gSDLUIState.frameHeight = frameHeight;
        gSDLUIState.sceneWidth = sceneWidth;
        gSDLUIState.sceneHeight = sceneHeight;
        gSDLUIState.scale = scale;
    }
    if(!changed)
        return;

    const Uint32 initialized = SDL_WasInit(0);
    char message[256];
    std::snprintf(
        message, sizeof(message),
        "viewport frame=%dx%d scene=%dx%d scale=%.3f events=%d video=%d "
        "audio=%d ticks=%u",
        frameWidth, frameHeight, sceneWidth, sceneHeight, scale,
        (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        (initialized & SDL_INIT_VIDEO) ? 1 : 0,
        (initialized & SDL_INIT_AUDIO) ? 1 : 0,
        static_cast<unsigned>(SDL_GetTicks()));
    LogSDLUI(message);
}

void TVPSDLUIRecordGameMenuCreated(int sceneWidth, int sceneHeight,
                                   float uiScale, int rootWidth,
                                   int rootHeight, int handlerWidth,
                                   int handlerHeight,
                                   int handlerInactiveOpacity) {
    TVPSDLInitializeRuntime();
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        gSDLUIState.gameMenuCreated = true;
        gSDLUIState.gameMenuShrinked = false;
        gSDLUIState.gameMenuHitted = false;
    }

    const Uint32 initialized = SDL_WasInit(0);
    char message[384];
    std::snprintf(
        message, sizeof(message),
        "game-menu create scene=%dx%d uiScale=%.3f root=%dx%d "
        "handler=%dx%d inactiveOpacity=%d events=%d video=%d audio=%d "
        "ticks=%u",
        sceneWidth, sceneHeight, uiScale, rootWidth, rootHeight, handlerWidth,
        handlerHeight, handlerInactiveOpacity,
        (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        (initialized & SDL_INIT_VIDEO) ? 1 : 0,
        (initialized & SDL_INIT_AUDIO) ? 1 : 0,
        static_cast<unsigned>(SDL_GetTicks()));
    LogSDLUI(message);
}

void TVPSDLUIRecordGameMenuState(const char *eventName, bool shrinked,
                                 bool hitted, float rootX, float rootY,
                                 float rootWidth, float rootHeight,
                                 float handlerX, float handlerY,
                                 float handlerWidth, float handlerHeight,
                                 bool mouseIcon, float duration) {
    TVPSDLInitializeRuntime();
    const uint64_t sequence =
        gSDLUIEventSequence.fetch_add(1, std::memory_order_relaxed) + 1;
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        gSDLUIState.gameMenuCreated = true;
        gSDLUIState.gameMenuShrinked = shrinked;
        gSDLUIState.gameMenuHitted = hitted;
        gSDLUIState.gameMenuMouseIcon = mouseIcon;
    }

    const Uint32 initialized = SDL_WasInit(0);
    char message[640];
    std::snprintf(
        message, sizeof(message),
        "game-menu state #%llu event=%s shrinked=%s hitted=%s mouseIcon=%s "
        "root=%.1f,%.1f %.1fx%.1f handler=%.1f,%.1f %.1fx%.1f "
        "duration=%.3f events=%d video=%d audio=%d ticks=%u",
        static_cast<unsigned long long>(sequence),
        eventName ? eventName : "", BoolString(shrinked),
        BoolString(hitted), BoolString(mouseIcon), rootX, rootY, rootWidth,
        rootHeight, handlerX, handlerY, handlerWidth, handlerHeight, duration,
        (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        (initialized & SDL_INIT_VIDEO) ? 1 : 0,
        (initialized & SDL_INIT_AUDIO) ? 1 : 0,
        static_cast<unsigned>(SDL_GetTicks()));
    LogSDLUI(message);
}

void TVPSDLUIRecordGameMenuAction(const char *actionName, bool shrinked,
                                  bool mouseIcon) {
    TVPSDLInitializeRuntime();
    const uint64_t sequence =
        gSDLUIMenuActionSequence.fetch_add(1, std::memory_order_relaxed) + 1;
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        gSDLUIState.gameMenuShrinked = shrinked;
        gSDLUIState.gameMenuMouseIcon = mouseIcon;
    }

    const Uint32 initialized = SDL_WasInit(0);
    char message[256];
    std::snprintf(
        message, sizeof(message),
        "game-menu action #%llu action=%s shrinked=%s mouseIcon=%s "
        "events=%d video=%d audio=%d ticks=%u",
        static_cast<unsigned long long>(sequence),
        actionName ? actionName : "", BoolString(shrinked),
        BoolString(mouseIcon), (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        (initialized & SDL_INIT_VIDEO) ? 1 : 0,
        (initialized & SDL_INIT_AUDIO) ? 1 : 0,
        static_cast<unsigned>(SDL_GetTicks()));
    LogSDLUI(message);
}
