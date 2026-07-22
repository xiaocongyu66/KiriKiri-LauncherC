#include "SDLUIManager.h"

#include "NativeLog.h"
#include "SDLGameManager.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

struct TVPSDLUITemplate {
    std::string name;
    std::string csb;
    std::string csd;
};

struct TVPSDLUIRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct TVPSDLUIGameMenuButton {
    std::string id;
    std::string action;
    std::string icon;
    TVPSDLUIRect localRect;
    bool enabled = true;
};

struct TVPSDLUIGameMenuQueuedAction {
    std::string action;
    std::string source;
    float sceneX = 0.0f;
    float sceneY = 0.0f;
    int pointerId = -1;
    Uint64 ticks = 0;
    uint64_t sequence = 0;
};

struct TVPSDLUIGameMenuTouchState {
    bool active = false;
    int pointerId = -1;
    std::string hitId;
    std::string action;
    float sceneX = 0.0f;
    float sceneY = 0.0f;
    Uint64 ticks = 0;
};

struct TVPSDLUIMessageBoxState {
    bool visible = false;
    uint64_t session = 0;
    std::string caption;
    std::string text;
    std::vector<std::string> buttons;
};

struct TVPSDLUIProgressState {
    bool visible = false;
    uint64_t session = 0;
    std::string title;
    std::string content;
    std::string progressText1;
    std::string progressText2;
    float percent1 = 0.0f;
    float percent2 = 0.0f;
    bool progress2Visible = true;
    std::vector<std::string> buttons;
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
    float gameMenuUiScale = 1.0f;
    TVPSDLUIRect gameMenuRootRect;
    TVPSDLUIRect gameMenuHandlerRect;
    int gameMenuHandlerInactiveOpacity = 0;
    std::vector<TVPSDLUIGameMenuButton> gameMenuButtons;
    std::deque<TVPSDLUIGameMenuQueuedAction> gameMenuActionQueue;
    TVPSDLUIGameMenuTouchState gameMenuTouch;
    std::string lastQueuedAction;
    Uint64 lastQueuedTicks = 0;

    TVPSDLUIMessageBoxState messageBox;
    TVPSDLUIProgressState progress;
};

std::mutex gSDLUIMutex;
TVPSDLUIState gSDLUIState;
std::atomic_uint64_t gSDLUIEventSequence{0};
std::atomic_uint64_t gSDLUIMenuActionSequence{0};
std::atomic_uint64_t gSDLUIInputSequence{0};
std::atomic_uint64_t gSDLUIMenuQueuedActionSequence{0};
std::atomic_uint64_t gSDLUIMenuDequeuedActionSequence{0};
std::atomic_uint64_t gSDLUIMessageActionSequence{0};
std::atomic_uint64_t gSDLUIProgressUpdateSequence{0};

std::string SafeString(const char *value) {
    return value ? std::string(value) : std::string();
}

const char *BoolString(bool value) { return value ? "1" : "0"; }

std::string ShortLogString(const std::string &value, size_t maxChars = 96) {
    if(value.size() <= maxChars)
        return value;
    return value.substr(0, maxChars) + "...";
}

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

std::string JoinStrings(const std::vector<std::string> &values) {
    std::string result;
    for(size_t i = 0; i < values.size(); ++i) {
        if(i)
            result += "|";
        result += ShortLogString(values[i], 48);
    }
    return result;
}

void EnsureDefaultGameMenuButtonsLocked();

TVPSDLUIRect ScaleRect(const TVPSDLUIRect &rect, float scale) {
    return { rect.x * scale, rect.y * scale, rect.width * scale,
             rect.height * scale };
}

TVPSDLUIRect RootLocalToScene(const TVPSDLUIRect &root,
                              const TVPSDLUIRect &localRect, float scale) {
    return { (root.x + localRect.x) * scale,
             (root.y + localRect.y) * scale, localRect.width * scale,
             localRect.height * scale };
}

std::string FormatRect(const TVPSDLUIRect &rect) {
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%.1f,%.1f %.1fx%.1f", rect.x,
                  rect.y, rect.width, rect.height);
    return buffer;
}

std::string FormatButtonRects(const TVPSDLUIState &state) {
    std::string result;
    for(size_t i = 0; i < state.gameMenuButtons.size(); ++i) {
        const auto &button = state.gameMenuButtons[i];
        const TVPSDLUIRect sceneRect = RootLocalToScene(
            state.gameMenuRootRect, button.localRect,
            state.gameMenuUiScale);
        if(i)
            result += ";";
        result += button.id;
        result += "=";
        result += button.action;
        result += "@";
        result += FormatRect(sceneRect);
        result += button.enabled ? ":on" : ":off";
    }
    return result;
}

bool IsHighFrequencyTouch(const char *eventName) {
    return eventName && std::strcmp(eventName, "touch-move") == 0;
}

bool ShouldLogTouch(uint64_t sequence, const char *eventName,
                    const std::string &hitId) {
    if(IsHighFrequencyTouch(eventName)) {
        if(!hitId.empty() && hitId != "none")
            return sequence <= 8 || (sequence % 32) == 0;
        return sequence <= 8 || (sequence % 256) == 0;
    }
    return true;
}

bool RectContainsPoint(const TVPSDLUIRect &rect, float x, float y) {
    return x >= rect.x && y >= rect.y && x <= rect.x + rect.width &&
        y <= rect.y + rect.height;
}

bool ConvertFrameToSceneLocked(float frameX, float frameY, float &sceneX,
                               float &sceneY) {
    const float scale = gSDLUIState.scale > 0.0f ? gSDLUIState.scale
                                                 : gSDLUIState.gameMenuUiScale;
    if(scale <= 0.0f)
        return false;
    const int frameHeight = gSDLUIState.frameHeight > 0
        ? gSDLUIState.frameHeight
        : static_cast<int>(gSDLUIState.sceneHeight * scale);
    if(frameHeight <= 0)
        return false;
    sceneX = frameX / scale;
    sceneY = (static_cast<float>(frameHeight) - frameY) / scale;
    return true;
}

std::string HitTestGameMenuLocked(float sceneX, float sceneY,
                                  std::string &actionName) {
    actionName.clear();
    if(!gSDLUIState.gameMenuCreated)
        return "none";

    EnsureDefaultGameMenuButtonsLocked();

    const bool menuExpanded = !gSDLUIState.gameMenuShrinked;
    if(menuExpanded) {
        for(const auto &button : gSDLUIState.gameMenuButtons) {
            if(!button.enabled)
                continue;
            const TVPSDLUIRect sceneRect = RootLocalToScene(
                gSDLUIState.gameMenuRootRect, button.localRect,
                gSDLUIState.gameMenuUiScale);
            if(RectContainsPoint(sceneRect, sceneX, sceneY)) {
                actionName = button.action;
                return button.id;
            }
        }
    }

    const TVPSDLUIRect handlerScene = RootLocalToScene(
        gSDLUIState.gameMenuRootRect, gSDLUIState.gameMenuHandlerRect,
        gSDLUIState.gameMenuUiScale);
    if(RectContainsPoint(handlerScene, sceneX, sceneY)) {
        actionName = "toggle";
        return "handler";
    }

    const TVPSDLUIRect rootScene =
        ScaleRect(gSDLUIState.gameMenuRootRect, gSDLUIState.gameMenuUiScale);
    if(menuExpanded && RectContainsPoint(rootScene, sceneX, sceneY)) {
        actionName = "shrink";
        return "root";
    }

    return "none";
}

bool QueueGameMenuActionLocked(const std::string &actionName,
                               const std::string &source, float sceneX,
                               float sceneY, int pointerId,
                               const Uint64 nowTicks, uint64_t &sequence,
                               bool &deduped) {
    sequence = 0;
    deduped = false;
    if(actionName.empty())
        return false;

    if(gSDLUIState.lastQueuedAction == actionName &&
       nowTicks - gSDLUIState.lastQueuedTicks < 120) {
        deduped = true;
        return false;
    }

    sequence =
        gSDLUIMenuQueuedActionSequence.fetch_add(1, std::memory_order_relaxed) +
        1;
    gSDLUIState.gameMenuActionQueue.push_back(
        { actionName, source, sceneX, sceneY, pointerId, nowTicks, sequence });
    gSDLUIState.lastQueuedAction = actionName;
    gSDLUIState.lastQueuedTicks = nowTicks;
    return true;
}

void EnsureDefaultGameMenuButtonsLocked() {
    if(!gSDLUIState.gameMenuButtons.empty())
        return;

    const float itemWidth = 144.0f;
    const float itemHeight = 130.0f;
    gSDLUIState.gameMenuButtons = {
        { "btn_gamemenu", "game-menu", "img/menu_icon.png",
          { 0.0f, 0.0f, itemWidth, itemHeight }, true },
        { "btn_window", "window-manager", "img/windows_icon.png",
          { itemWidth, 0.0f, itemWidth, itemHeight }, true },
        { "btn_mousemode", "mouse-mode", "img/mouse_icon.png",
          { itemWidth * 2.0f, 0.0f, itemWidth, itemHeight }, true },
        { "btn_keyboard", "keyboard", "img/keyboard_icon.png",
          { itemWidth * 3.0f, 0.0f, itemWidth, itemHeight }, true },
        { "btn_exit", "exit", "img/exit_icon.png",
          { itemWidth * 4.0f, 0.0f, itemWidth, itemHeight }, true },
    };
}

void LogGameMenuRenderIntent(const TVPSDLUIState &state,
                             const char *eventName, uint64_t sequence,
                             Uint32 initialized) {
    if(!state.gameMenuCreated)
        return;

    const TVPSDLUIRect rootScene =
        ScaleRect(state.gameMenuRootRect, state.gameMenuUiScale);
    const TVPSDLUIRect handlerScene = RootLocalToScene(
        state.gameMenuRootRect, state.gameMenuHandlerRect,
        state.gameMenuUiScale);
    const std::string buttons = FormatButtonRects(state);

    char message[1536];
    std::snprintf(
        message, sizeof(message),
        "game-menu render-intent #%llu event=%s backend=sdlui-runtime-shell "
        "visible=%d shrinked=%s hitted=%s mouseIcon=%s scene=%dx%d "
        "scale=%.3f rootScene=%s handlerScene=%s buttons=%zu [%s] "
        "events=%d video=%d audio=%d ticks=%u",
        static_cast<unsigned long long>(sequence), eventName ? eventName : "",
        state.gameMenuShrinked ? 0 : 1, BoolString(state.gameMenuShrinked),
        BoolString(state.gameMenuHitted), BoolString(state.gameMenuMouseIcon),
        state.sceneWidth, state.sceneHeight, state.gameMenuUiScale,
        FormatRect(rootScene).c_str(), FormatRect(handlerScene).c_str(),
        state.gameMenuButtons.size(), buttons.c_str(),
        (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        (initialized & SDL_INIT_VIDEO) ? 1 : 0,
        (initialized & SDL_INIT_AUDIO) ? 1 : 0,
        static_cast<unsigned>(SDL_GetTicks()));
    LogSDLUI(message);
}

std::vector<TVPSDLUITemplate> BuildLegacyTemplates(const std::string &root) {
    const std::string prefix =
        root.empty() || root == "." ? std::string() : root + "/";
    return {
        { "loading-console", "ui/ConsoleWindow.sdlui",
          prefix + "out_ui/ConsoleWindow.sdlui" },
        { "archive-repacker", prefix + "ui/ArchiveRepacker.csb",
          prefix + "out_ui/ArchiveRepacker.csd" },
        { "bottom-bar", prefix + "ui/BottomBar.csb",
          prefix + "out_ui/BottomBar.csd" },
        { "bottom-bar-text-input", prefix + "ui/BottomBarTextInput.csb",
          prefix + "out_ui/BottomBarTextInput.csd" },
        { "check-list-dialog", prefix + "ui/CheckListDialog.csb",
          prefix + "out_ui/CheckListDialog.csd" },
        { "file-item", prefix + "ui/FileItem.csb",
          prefix + "out_ui/FileItem.csd" },
        { "file-manage-menu", prefix + "ui/FileManageMenu.csb",
          prefix + "out_ui/FileManageMenu.csd" },
        { "game-menu", prefix + "ui/GameMenu.csb",
          prefix + "out_ui/GameMenu.csd" },
        { "key-select", prefix + "ui/KeySelect.csb",
          prefix + "out_ui/KeySelect.csd" },
        { "list-item", prefix + "ui/ListItem.csb",
          prefix + "out_ui/ListItem.csd" },
        { "list-view", prefix + "ui/ListView.csb",
          prefix + "out_ui/ListView.csd" },
        { "main-file-selector", prefix + "ui/MainFileSelector.csb",
          prefix + "out_ui/MainFileSelector.csd" },
        { "media-player-body", prefix + "ui/MediaPlayerBody.csb",
          prefix + "out_ui/MediaPlayerBody.csd" },
        { "media-player-foot", prefix + "ui/MediaPlayerFoot.csb",
          prefix + "out_ui/MediaPlayerFoot.csd" },
        { "media-player-navi", prefix + "ui/MediaPlayerNavi.csb",
          prefix + "out_ui/MediaPlayerNavi.csd" },
        { "menu-list", prefix + "ui/MenuList.csb",
          prefix + "out_ui/MenuList.csd" },
        { "message-box", prefix + "ui/MessageBox.csb",
          prefix + "out_ui/MessageBox.csd" },
        { "navi-bar", prefix + "ui/NaviBar.csb",
          prefix + "out_ui/NaviBar.csd" },
        { "navi-bar-with-menu", prefix + "ui/NaviBarWithMenu.csb",
          prefix + "out_ui/NaviBarWithMenu.csd" },
        { "progress-box", prefix + "ui/ProgressBox.csb",
          prefix + "out_ui/ProgressBox.csd" },
        { "recent-list-item", prefix + "ui/RecentListItem.csb",
          prefix + "out_ui/RecentListItem.csd" },
        { "select-list", prefix + "ui/SelectList.csb",
          prefix + "out_ui/SelectList.csd" },
        { "select-list-item", prefix + "ui/SelectListItem.csb",
          prefix + "out_ui/SelectListItem.csd" },
        { "table-view", prefix + "ui/TableView.csb",
          prefix + "out_ui/TableView.csd" },
        { "text-pair-input", prefix + "ui/TextPairInput.csb",
          prefix + "out_ui/TextPairInput.csd" },
        { "window-manager", prefix + "ui/WinMgrOverlay.csb",
          prefix + "out_ui/WinMgrOverlay.csd" },
        { "comctrl-check-box-item", prefix + "ui/comctrl/CheckBoxItem.csb",
          prefix + "out_ui/comctrl/CheckBoxItem.csd" },
        { "comctrl-select-list-item",
          prefix + "ui/comctrl/SelectListItem.csb",
          prefix + "out_ui/comctrl/SelectListItem.csd" },
        { "comctrl-seperate-item", prefix + "ui/comctrl/SeperateItem.csb",
          prefix + "out_ui/comctrl/SeperateItem.csd" },
        { "comctrl-slider-icon-item",
          prefix + "ui/comctrl/SliderIconItem.csb",
          prefix + "out_ui/comctrl/SliderIconItem.csd" },
        { "comctrl-slider-text-item",
          prefix + "ui/comctrl/SliderTextItem.csb",
          prefix + "out_ui/comctrl/SliderTextItem.csd" },
        { "comctrl-sub-dir-item", prefix + "ui/comctrl/SubDirItem.csb",
          prefix + "out_ui/comctrl/SubDirItem.csd" },
        { "help-all-tips", prefix + "ui/help/AllTips.csb",
          prefix + "out_ui/help/AllTips.csd" },
        { "help-mouse-mode-tips", prefix + "ui/help/MouseModeTips.csb",
          prefix + "out_ui/help/MouseModeTips.csd" },
        { "help-screen-mode-tips", prefix + "ui/help/ScreenModeTips.csb",
          prefix + "out_ui/help/ScreenModeTips.csd" },
        { "help-touch-mode-tips", prefix + "ui/help/TouchModeTips.csb",
          prefix + "out_ui/help/TouchModeTips.csd" },
    };
}

} // namespace

void TVPSDLUIRegisterLegacyRuntimeUIAssets(const char *sourceRoot,
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
    char message[2048];
    std::snprintf(
        message, sizeof(message),
        "assets source=%s runtime=%s templates=%zu names=%s imageRoot=img "
        "localeRoot=locale font=%s reuse=legacy-runtime-ui events=%d "
        "video=%d audio=%d ticks=%u",
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

void TVPSDLUIRecordAndroidTouch(const char *eventName, float frameX,
                                float frameY, int pointerId, bool active) {
    TVPSDLInitializeRuntime();
    const uint64_t inputSequence =
        gSDLUIInputSequence.fetch_add(1, std::memory_order_relaxed) + 1;

    float sceneX = 0.0f;
    float sceneY = 0.0f;
    bool converted = false;
    std::string hitId;
    std::string hitAction;
    uint64_t queuedSequence = 0;
    bool queued = false;
    bool deduped = false;
    bool mirrorOnly = false;
    size_t backlog = 0;

    const Uint64 nowTicks = SDL_GetTicks();
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        converted = ConvertFrameToSceneLocked(frameX, frameY, sceneX, sceneY);
        if(converted) {
            hitId = HitTestGameMenuLocked(sceneX, sceneY, hitAction);
        } else {
            hitId = "unmapped";
        }

        if(eventName && std::strcmp(eventName, "touch-begin") == 0) {
            gSDLUIState.gameMenuTouch = {};
            gSDLUIState.gameMenuTouch.active = active && !hitAction.empty();
            gSDLUIState.gameMenuTouch.pointerId = pointerId;
            gSDLUIState.gameMenuTouch.hitId = hitId;
            gSDLUIState.gameMenuTouch.action = hitAction;
            gSDLUIState.gameMenuTouch.sceneX = sceneX;
            gSDLUIState.gameMenuTouch.sceneY = sceneY;
            gSDLUIState.gameMenuTouch.ticks = nowTicks;
        } else if(eventName && std::strcmp(eventName, "touch-end") == 0) {
            const bool samePointer =
                gSDLUIState.gameMenuTouch.active &&
                gSDLUIState.gameMenuTouch.pointerId == pointerId;
            const bool sameAction =
                samePointer && !hitAction.empty() &&
                hitAction == gSDLUIState.gameMenuTouch.action;
            const bool stillOwnedByLegacyDrag =
                gSDLUIState.gameMenuTouch.hitId == "handler" ||
                gSDLUIState.gameMenuTouch.hitId == "root";
            mirrorOnly = sameAction && stillOwnedByLegacyDrag;
            if(sameAction && !stillOwnedByLegacyDrag) {
                queued = QueueGameMenuActionLocked(
                    hitAction, "android-touch", sceneX, sceneY, pointerId,
                    nowTicks, queuedSequence, deduped);
            }
            if(samePointer)
                gSDLUIState.gameMenuTouch = {};
        } else if(eventName &&
                  (std::strcmp(eventName, "touch-cancel") == 0 ||
                   std::strcmp(eventName, "touch-cancel-empty") == 0)) {
            if(gSDLUIState.gameMenuTouch.pointerId == pointerId ||
               pointerId < 0)
                gSDLUIState.gameMenuTouch = {};
        }

        backlog = gSDLUIState.gameMenuActionQueue.size();
    }

    if(!ShouldLogTouch(inputSequence, eventName, hitId) && !queued &&
       !deduped)
        return;

    const Uint32 initialized = SDL_WasInit(0);
    char message[640];
    std::snprintf(
        message, sizeof(message),
        "touch #%llu event=%s pointer=%d frame=%.1f,%.1f scene=%.1f,%.1f "
        "mapped=%d hit=%s action=%s active=%d queued=%d deduped=%d "
        "mirrorOnly=%d queueSeq=%llu backlog=%zu events=%d video=%d "
        "audio=%d ticks=%u",
        static_cast<unsigned long long>(inputSequence),
        eventName ? eventName : "", pointerId, frameX, frameY, sceneX, sceneY,
        converted ? 1 : 0, hitId.c_str(), hitAction.c_str(), active ? 1 : 0,
        queued ? 1 : 0, deduped ? 1 : 0, mirrorOnly ? 1 : 0,
        static_cast<unsigned long long>(queuedSequence), backlog,
        (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        (initialized & SDL_INIT_VIDEO) ? 1 : 0,
        (initialized & SDL_INIT_AUDIO) ? 1 : 0,
        static_cast<unsigned>(nowTicks));
    LogSDLUI(message);
}

void TVPSDLUIRecordGameMenuCreated(int sceneWidth, int sceneHeight,
                                   float uiScale, int rootWidth,
                                   int rootHeight, int handlerWidth,
                                   int handlerHeight,
                                   int handlerInactiveOpacity) {
    TVPSDLInitializeRuntime();
    TVPSDLUIState snapshot;
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        gSDLUIState.gameMenuCreated = true;
        gSDLUIState.gameMenuShrinked = false;
        gSDLUIState.gameMenuHitted = false;
        gSDLUIState.sceneWidth = sceneWidth;
        gSDLUIState.sceneHeight = sceneHeight;
        gSDLUIState.gameMenuUiScale = uiScale > 0.0f ? uiScale : 1.0f;
        gSDLUIState.gameMenuRootRect = {
            0.0f, 0.0f, static_cast<float>(rootWidth),
            static_cast<float>(rootHeight),
        };
        gSDLUIState.gameMenuHandlerRect = {
            static_cast<float>(std::max(0, rootWidth - handlerWidth)),
            static_cast<float>(rootHeight),
            static_cast<float>(handlerWidth),
            static_cast<float>(handlerHeight),
        };
        gSDLUIState.gameMenuHandlerInactiveOpacity =
            handlerInactiveOpacity;
        EnsureDefaultGameMenuButtonsLocked();
        snapshot = gSDLUIState;
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
    LogGameMenuRenderIntent(snapshot, "create", 0, initialized);
}

void TVPSDLUIRecordGameMenuButton(const char *id, const char *actionName,
                                  float localX, float localY, float width,
                                  float height, const char *iconPath,
                                  bool enabled) {
    TVPSDLInitializeRuntime();
    if(width <= 0.0f || height <= 0.0f)
        return;

    const std::string safeId = SafeString(id);
    if(safeId.empty())
        return;

    TVPSDLUIState snapshot;
    size_t buttonCount = 0;
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        auto it = std::find_if(
            gSDLUIState.gameMenuButtons.begin(),
            gSDLUIState.gameMenuButtons.end(),
            [&safeId](const TVPSDLUIGameMenuButton &button) {
                return button.id == safeId;
            });
        TVPSDLUIGameMenuButton button{
            safeId,
            SafeString(actionName),
            SafeString(iconPath),
            { localX, localY, width, height },
            enabled,
        };
        if(it == gSDLUIState.gameMenuButtons.end()) {
            gSDLUIState.gameMenuButtons.emplace_back(std::move(button));
        } else {
            *it = std::move(button);
        }
        buttonCount = gSDLUIState.gameMenuButtons.size();
        snapshot = gSDLUIState;
    }

    const Uint32 initialized = SDL_WasInit(0);
    char message[512];
    std::snprintf(
        message, sizeof(message),
        "game-menu button id=%s action=%s local=%.1f,%.1f %.1fx%.1f "
        "icon=%s enabled=%d total=%zu events=%d video=%d audio=%d ticks=%u",
        safeId.c_str(), actionName ? actionName : "", localX, localY, width,
        height, iconPath ? iconPath : "", enabled ? 1 : 0, buttonCount,
        (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        (initialized & SDL_INIT_VIDEO) ? 1 : 0,
        (initialized & SDL_INIT_AUDIO) ? 1 : 0,
        static_cast<unsigned>(SDL_GetTicks()));
    LogSDLUI(message);
    LogGameMenuRenderIntent(snapshot, "button-layout", 0, initialized);
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
    TVPSDLUIState snapshot;
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        gSDLUIState.gameMenuCreated = true;
        gSDLUIState.gameMenuShrinked = shrinked;
        gSDLUIState.gameMenuHitted = hitted;
        gSDLUIState.gameMenuMouseIcon = mouseIcon;
        gSDLUIState.gameMenuRootRect = {
            rootX, rootY, rootWidth, rootHeight,
        };
        gSDLUIState.gameMenuHandlerRect = {
            handlerX, handlerY, handlerWidth, handlerHeight,
        };
        EnsureDefaultGameMenuButtonsLocked();
        snapshot = gSDLUIState;
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
    LogGameMenuRenderIntent(snapshot, eventName, sequence, initialized);
}

void TVPSDLUIQueueGameMenuAction(const char *actionName, const char *source,
                                 float sceneX, float sceneY, int pointerId) {
    TVPSDLInitializeRuntime();
    const Uint64 nowTicks = SDL_GetTicks();
    uint64_t sequence = 0;
    bool deduped = false;
    bool queued = false;
    size_t backlog = 0;
    const std::string safeAction = SafeString(actionName);
    const std::string safeSource = SafeString(source);

    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        queued = QueueGameMenuActionLocked(safeAction, safeSource, sceneX,
                                           sceneY, pointerId, nowTicks,
                                           sequence, deduped);
        backlog = gSDLUIState.gameMenuActionQueue.size();
    }

    const Uint32 initialized = SDL_WasInit(0);
    char message[512];
    std::snprintf(
        message, sizeof(message),
        "game-menu queue action=%s source=%s queued=%d deduped=%d "
        "queueSeq=%llu pointer=%d scene=%.1f,%.1f backlog=%zu events=%d "
        "video=%d audio=%d ticks=%u",
        safeAction.c_str(), safeSource.c_str(), queued ? 1 : 0,
        deduped ? 1 : 0, static_cast<unsigned long long>(sequence), pointerId,
        sceneX, sceneY, backlog, (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        (initialized & SDL_INIT_VIDEO) ? 1 : 0,
        (initialized & SDL_INIT_AUDIO) ? 1 : 0,
        static_cast<unsigned>(nowTicks));
    LogSDLUI(message);
}

bool TVPSDLUIPollGameMenuAction(char *actionName, size_t actionNameSize) {
    TVPSDLInitializeRuntime();
    if(!actionName || actionNameSize == 0)
        return false;

    TVPSDLUIGameMenuQueuedAction action;
    size_t backlog = 0;
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        if(gSDLUIState.gameMenuActionQueue.empty())
            return false;
        action = std::move(gSDLUIState.gameMenuActionQueue.front());
        gSDLUIState.gameMenuActionQueue.pop_front();
        backlog = gSDLUIState.gameMenuActionQueue.size();
    }

    std::snprintf(actionName, actionNameSize, "%s", action.action.c_str());

    const uint64_t sequence =
        gSDLUIMenuDequeuedActionSequence.fetch_add(
            1, std::memory_order_relaxed) +
        1;
    const Uint32 initialized = SDL_WasInit(0);
    char message[512];
    std::snprintf(
        message, sizeof(message),
        "game-menu dequeue #%llu action=%s source=%s queueSeq=%llu pointer=%d "
        "scene=%.1f,%.1f ageMs=%u backlog=%zu events=%d video=%d audio=%d "
        "ticks=%u",
        static_cast<unsigned long long>(sequence), action.action.c_str(),
        action.source.c_str(), static_cast<unsigned long long>(action.sequence),
        action.pointerId, action.sceneX, action.sceneY,
        static_cast<unsigned>(SDL_GetTicks() - action.ticks), backlog,
        (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        (initialized & SDL_INIT_VIDEO) ? 1 : 0,
        (initialized & SDL_INIT_AUDIO) ? 1 : 0,
        static_cast<unsigned>(SDL_GetTicks()));
    LogSDLUI(message);
    return true;
}

void TVPSDLUIRecordGameMenuAction(const char *actionName, bool shrinked,
                                  bool mouseIcon) {
    TVPSDLInitializeRuntime();
    const uint64_t sequence =
        gSDLUIMenuActionSequence.fetch_add(1, std::memory_order_relaxed) + 1;
    TVPSDLUIState snapshot;
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        gSDLUIState.gameMenuShrinked = shrinked;
        gSDLUIState.gameMenuMouseIcon = mouseIcon;
        snapshot = gSDLUIState;
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
    LogGameMenuRenderIntent(snapshot, actionName, sequence, initialized);
}

void TVPSDLUIRecordMessageBoxShow(const char *caption, const char *text,
                                  int buttonCount,
                                  const char *const *buttonTexts) {
    TVPSDLInitializeRuntime();
    std::vector<std::string> buttons;
    if(buttonCount > 0 && buttonTexts) {
        buttons.reserve(static_cast<size_t>(buttonCount));
        for(int i = 0; i < buttonCount; ++i) {
            buttons.emplace_back(SafeString(buttonTexts[i]));
        }
    }

    uint64_t session = 0;
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        gSDLUIState.messageBox.visible = true;
        session = ++gSDLUIState.messageBox.session;
        gSDLUIState.messageBox.caption = SafeString(caption);
        gSDLUIState.messageBox.text = SafeString(text);
        gSDLUIState.messageBox.buttons = buttons;
    }

    const Uint32 initialized = SDL_WasInit(0);
    const std::string safeCaption = ShortLogString(SafeString(caption));
    const std::string safeText = ShortLogString(SafeString(text));
    const std::string joinedButtons = JoinStrings(buttons);
    char message[1024];
    std::snprintf(
        message, sizeof(message),
        "message-box show session=%llu template=message-box captionLen=%zu "
        "textLen=%zu buttons=%zu [%s] caption=%s text=%s events=%d video=%d "
        "audio=%d ticks=%u",
        static_cast<unsigned long long>(session), SafeString(caption).size(),
        SafeString(text).size(), buttons.size(), joinedButtons.c_str(),
        safeCaption.c_str(), safeText.c_str(),
        (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        (initialized & SDL_INIT_VIDEO) ? 1 : 0,
        (initialized & SDL_INIT_AUDIO) ? 1 : 0,
        static_cast<unsigned>(SDL_GetTicks()));
    LogSDLUI(message);
}

void TVPSDLUIRecordMessageBoxAction(int buttonIndex, const char *source) {
    TVPSDLInitializeRuntime();
    const uint64_t sequence =
        gSDLUIMessageActionSequence.fetch_add(1, std::memory_order_relaxed) +
        1;
    uint64_t session = 0;
    size_t buttonCount = 0;
    std::string buttonText;
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        session = gSDLUIState.messageBox.session;
        buttonCount = gSDLUIState.messageBox.buttons.size();
        if(buttonIndex >= 0 &&
           static_cast<size_t>(buttonIndex) <
               gSDLUIState.messageBox.buttons.size()) {
            buttonText = gSDLUIState.messageBox.buttons[buttonIndex];
        }
    }

    const Uint32 initialized = SDL_WasInit(0);
    char message[512];
    std::snprintf(
        message, sizeof(message),
        "message-box action #%llu session=%llu button=%d buttonText=%s "
        "buttonCount=%zu source=%s events=%d video=%d audio=%d ticks=%u",
        static_cast<unsigned long long>(sequence),
        static_cast<unsigned long long>(session), buttonIndex,
        ShortLogString(buttonText, 48).c_str(), buttonCount,
        source ? source : "", (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        (initialized & SDL_INIT_VIDEO) ? 1 : 0,
        (initialized & SDL_INIT_AUDIO) ? 1 : 0,
        static_cast<unsigned>(SDL_GetTicks()));
    LogSDLUI(message);
}

void TVPSDLUIRecordMessageBoxClose(const char *reason) {
    TVPSDLInitializeRuntime();
    uint64_t session = 0;
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        session = gSDLUIState.messageBox.session;
        gSDLUIState.messageBox.visible = false;
    }

    const Uint32 initialized = SDL_WasInit(0);
    char message[320];
    std::snprintf(message, sizeof(message),
                  "message-box close session=%llu reason=%s events=%d "
                  "video=%d audio=%d ticks=%u",
                  static_cast<unsigned long long>(session),
                  reason ? reason : "",
                  (initialized & SDL_INIT_EVENTS) ? 1 : 0,
                  (initialized & SDL_INIT_VIDEO) ? 1 : 0,
                  (initialized & SDL_INIT_AUDIO) ? 1 : 0,
                  static_cast<unsigned>(SDL_GetTicks()));
    LogSDLUI(message);
}

void TVPSDLUIRecordProgressShow(const char *source) {
    TVPSDLInitializeRuntime();
    uint64_t session = 0;
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        gSDLUIState.progress.visible = true;
        session = ++gSDLUIState.progress.session;
        gSDLUIState.progress.title.clear();
        gSDLUIState.progress.content.clear();
        gSDLUIState.progress.progressText1.clear();
        gSDLUIState.progress.progressText2.clear();
        gSDLUIState.progress.percent1 = 0.0f;
        gSDLUIState.progress.percent2 = 0.0f;
        gSDLUIState.progress.progress2Visible = true;
        gSDLUIState.progress.buttons.clear();
    }

    const Uint32 initialized = SDL_WasInit(0);
    char message[384];
    std::snprintf(message, sizeof(message),
                  "progress-box show session=%llu template=progress-box "
                  "source=%s events=%d video=%d audio=%d ticks=%u",
                  static_cast<unsigned long long>(session),
                  source ? source : "",
                  (initialized & SDL_INIT_EVENTS) ? 1 : 0,
                  (initialized & SDL_INIT_VIDEO) ? 1 : 0,
                  (initialized & SDL_INIT_AUDIO) ? 1 : 0,
                  static_cast<unsigned>(SDL_GetTicks()));
    LogSDLUI(message);
}

void TVPSDLUIRecordProgressButtons(int buttonCount,
                                   const char *const *buttonTexts) {
    TVPSDLInitializeRuntime();
    std::vector<std::string> buttons;
    if(buttonCount > 0 && buttonTexts) {
        buttons.reserve(static_cast<size_t>(buttonCount));
        for(int i = 0; i < buttonCount; ++i) {
            buttons.emplace_back(SafeString(buttonTexts[i]));
        }
    }
    uint64_t session = 0;
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        session = gSDLUIState.progress.session;
        gSDLUIState.progress.buttons = buttons;
    }

    const Uint32 initialized = SDL_WasInit(0);
    const std::string joinedButtons = JoinStrings(buttons);
    char message[512];
    std::snprintf(message, sizeof(message),
                  "progress-box buttons session=%llu count=%zu [%s] "
                  "events=%d video=%d audio=%d ticks=%u",
                  static_cast<unsigned long long>(session), buttons.size(),
                  joinedButtons.c_str(),
                  (initialized & SDL_INIT_EVENTS) ? 1 : 0,
                  (initialized & SDL_INIT_VIDEO) ? 1 : 0,
                  (initialized & SDL_INIT_AUDIO) ? 1 : 0,
                  static_cast<unsigned>(SDL_GetTicks()));
    LogSDLUI(message);
}

void TVPSDLUIRecordProgressText(const char *fieldName, const char *text) {
    TVPSDLInitializeRuntime();
    const std::string field = SafeString(fieldName);
    const std::string value = SafeString(text);
    uint64_t session = 0;
    const uint64_t sequence =
        gSDLUIProgressUpdateSequence.fetch_add(1, std::memory_order_relaxed) +
        1;
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        session = gSDLUIState.progress.session;
        if(field == "title")
            gSDLUIState.progress.title = value;
        else if(field == "content")
            gSDLUIState.progress.content = value;
        else if(field == "progress_text_1")
            gSDLUIState.progress.progressText1 = value;
        else if(field == "progress_text_2")
            gSDLUIState.progress.progressText2 = value;
    }

    const Uint32 initialized = SDL_WasInit(0);
    char message[512];
    std::snprintf(
        message, sizeof(message),
        "progress-box text #%llu session=%llu field=%s len=%zu value=%s "
        "events=%d video=%d audio=%d ticks=%u",
        static_cast<unsigned long long>(sequence),
        static_cast<unsigned long long>(session), field.c_str(), value.size(),
        ShortLogString(value).c_str(),
        (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        (initialized & SDL_INIT_VIDEO) ? 1 : 0,
        (initialized & SDL_INIT_AUDIO) ? 1 : 0,
        static_cast<unsigned>(SDL_GetTicks()));
    LogSDLUI(message);
}

void TVPSDLUIRecordProgressPercent(const char *fieldName, float percent) {
    TVPSDLInitializeRuntime();
    const std::string field = SafeString(fieldName);
    uint64_t session = 0;
    const uint64_t sequence =
        gSDLUIProgressUpdateSequence.fetch_add(1, std::memory_order_relaxed) +
        1;
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        session = gSDLUIState.progress.session;
        if(field == "percent_1")
            gSDLUIState.progress.percent1 = percent;
        else if(field == "percent_2")
            gSDLUIState.progress.percent2 = percent;
    }

    const Uint32 initialized = SDL_WasInit(0);
    char message[384];
    std::snprintf(message, sizeof(message),
                  "progress-box percent #%llu session=%llu field=%s "
                  "percent=%.3f events=%d video=%d audio=%d ticks=%u",
                  static_cast<unsigned long long>(sequence),
                  static_cast<unsigned long long>(session), field.c_str(),
                  percent, (initialized & SDL_INIT_EVENTS) ? 1 : 0,
                  (initialized & SDL_INIT_VIDEO) ? 1 : 0,
                  (initialized & SDL_INIT_AUDIO) ? 1 : 0,
                  static_cast<unsigned>(SDL_GetTicks()));
    LogSDLUI(message);
}

void TVPSDLUIRecordProgressVisible(const char *fieldName, bool visible) {
    TVPSDLInitializeRuntime();
    const std::string field = SafeString(fieldName);
    uint64_t session = 0;
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        session = gSDLUIState.progress.session;
        if(field == "progress_2")
            gSDLUIState.progress.progress2Visible = visible;
    }

    const Uint32 initialized = SDL_WasInit(0);
    char message[384];
    std::snprintf(message, sizeof(message),
                  "progress-box visible session=%llu field=%s visible=%d "
                  "events=%d video=%d audio=%d ticks=%u",
                  static_cast<unsigned long long>(session), field.c_str(),
                  visible ? 1 : 0,
                  (initialized & SDL_INIT_EVENTS) ? 1 : 0,
                  (initialized & SDL_INIT_VIDEO) ? 1 : 0,
                  (initialized & SDL_INIT_AUDIO) ? 1 : 0,
                  static_cast<unsigned>(SDL_GetTicks()));
    LogSDLUI(message);
}

void TVPSDLUIRecordProgressClose(const char *reason) {
    TVPSDLInitializeRuntime();
    uint64_t session = 0;
    {
        std::lock_guard<std::mutex> lock(gSDLUIMutex);
        session = gSDLUIState.progress.session;
        gSDLUIState.progress.visible = false;
    }

    const Uint32 initialized = SDL_WasInit(0);
    char message[320];
    std::snprintf(message, sizeof(message),
                  "progress-box close session=%llu reason=%s events=%d "
                  "video=%d audio=%d ticks=%u",
                  static_cast<unsigned long long>(session),
                  reason ? reason : "",
                  (initialized & SDL_INIT_EVENTS) ? 1 : 0,
                  (initialized & SDL_INIT_VIDEO) ? 1 : 0,
                  (initialized & SDL_INIT_AUDIO) ? 1 : 0,
                  static_cast<unsigned>(SDL_GetTicks()));
    LogSDLUI(message);
}
