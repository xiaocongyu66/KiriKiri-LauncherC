#include "tjsCommHead.h"

#include "NativeLog.h"
#include "runtime/RuntimePresenter.h"
#include "SDLPresentTypes.h"
#include "WindowImpl.h"
#include "RenderManager.h"
#include "TVPWindow.h"

#include <algorithm>
#include <mutex>
#include <string>

namespace {

std::mutex gSDLRuntimeWindowMutex;
tTJSNI_Window *gSDLRuntimeActiveWindow = nullptr;
void (*gSDLRuntimePostUpdate)() = nullptr;

class TVPSDLRuntimeWindowLayer final : public iWindowLayer {
public:
    explicit TVPSDLRuntimeWindowLayer(tTJSNI_Window *window)
        : Window(window) {
        Width = kTVPSDLFixedGameSurfaceWidth;
        Height = kTVPSDLFixedGameSurfaceHeight;
        std::lock_guard<std::mutex> lock(gSDLRuntimeWindowMutex);
        gSDLRuntimeActiveWindow = window;
    }

    ~TVPSDLRuntimeWindowLayer() {
        std::lock_guard<std::mutex> lock(gSDLRuntimeWindowMutex);
        if(gSDLRuntimeActiveWindow == Window)
            gSDLRuntimeActiveWindow = nullptr;
    }

    void SetPaintBoxSize(tjs_int w, tjs_int h) override {
        Width = w > 0 ? w : kTVPSDLFixedGameSurfaceWidth;
        Height = h > 0 ? h : kTVPSDLFixedGameSurfaceHeight;
    }

    bool GetFormEnabled() override { return true; }
    void SetDefaultMouseCursor() override {}
    void GetCursorPos(tjs_int &x, tjs_int &y) override {
        x = CursorX;
        y = CursorY;
    }
    void SetCursorPos(tjs_int x, tjs_int y) override {
        CursorX = x;
        CursorY = y;
    }
    void SetHintText(const ttstr &) override {}
    void SetAttentionPoint(tjs_int, tjs_int, const tTVPFont *) override {}
    void ZoomRectangle(tjs_int &, tjs_int &, tjs_int &, tjs_int &) override {}
    void BringToFront() override {}
    void ShowWindowAsModal() override {}
    bool GetVisible() override { return Visible; }
    void SetVisible(bool visible) override { Visible = visible; }
    const char *GetCaption() override { return Caption.c_str(); }
    void SetCaption(const std::string &caption) override { Caption = caption; }
    void SetWidth(tjs_int w) override { SetSize(w, Height); }
    void SetHeight(tjs_int h) override { SetSize(Width, h); }
    void SetSize(tjs_int w, tjs_int h) override {
        Width = w > 0 ? w : kTVPSDLFixedGameSurfaceWidth;
        Height = h > 0 ? h : kTVPSDLFixedGameSurfaceHeight;
    }
    void GetSize(tjs_int &w, tjs_int &h) override {
        w = Width;
        h = Height;
    }
    tjs_int GetWidth() const override { return Width; }
    tjs_int GetHeight() const override { return Height; }
    void GetWinSize(tjs_int &w, tjs_int &h) override {
        w = kTVPSDLFixedGameSurfaceWidth;
        h = kTVPSDLFixedGameSurfaceHeight;
    }
    void SetZoom(tjs_int numer, tjs_int denom) override {
        ZoomNumer = numer > 0 ? numer : 1;
        ZoomDenom = denom > 0 ? denom : 1;
    }

    void UpdateDrawBuffer(iTVPTexture2D *texture) override {
        if(!texture)
            return;
        TVPRuntimeTexturePresentRequest request;
        request.texture = texture;
        request.stage = "SDLRuntimeWindowLayer::UpdateDrawBuffer";
        request.layerWidth = Width;
        request.layerHeight = Height;
        request.frameProduced = true;
        tTVPRect dirty;
        if(texture->PeekDirtyRect(dirty)) {
            const tTVPRect fullRect(0, 0,
                                    static_cast<tjs_int>(texture->GetWidth()),
                                    static_cast<tjs_int>(texture->GetHeight()));
            dirty.clip(fullRect);
            if(!dirty.is_empty()) {
                request.hasDirtyRect = true;
                request.dirtyRect = { dirty.left, dirty.top,
                                      dirty.get_width(), dirty.get_height() };
            }
        }
        TVPRuntimePresentHostWindowTexture(
            static_cast<tTJSNI_BaseWindow *>(Window), request);
    }

    void InvalidateClose() override { delete this; }
    bool GetWindowActive() override { return Visible; }
    void Close() override { Visible = false; }
    void OnCloseQueryCalled(bool) override {}
    void InternalKeyDown(tjs_uint16, tjs_uint32) override {}
    void OnKeyUp(tjs_uint16, int) override {}
    void OnKeyPress(tjs_uint16, int, bool, bool) override {}
    tTVPImeMode GetDefaultImeMode() const override { return imDisable; }
    void SetImeMode(tTVPImeMode) override {}
    void ResetImeMode() override {}
    void UpdateWindow(tTVPUpdateType) override {}
    void SetVisibleFromScript(bool visible) override { SetVisible(visible); }
    void SetUseMouseKey(bool enabled) override { UseMouseKey = enabled; }
    bool GetUseMouseKey() const override { return UseMouseKey; }
    void ResetMouseVelocity() override {}
    void ResetTouchVelocity(tjs_int) override {}
    bool GetMouseVelocity(float &x, float &y, float &speed) const override {
        x = 0.0f;
        y = 0.0f;
        speed = 0.0f;
        return false;
    }
    void TickBeat() override {}
    cocos2d::Node *GetPrimaryArea() override { return nullptr; }

private:
    tTJSNI_Window *Window = nullptr;
    tjs_int Width = kTVPSDLFixedGameSurfaceWidth;
    tjs_int Height = kTVPSDLFixedGameSurfaceHeight;
    tjs_int CursorX = 0;
    tjs_int CursorY = 0;
    bool Visible = true;
    bool UseMouseKey = false;
    std::string Caption;
};

} // namespace

iWindowLayer *TVPCreateAndAddWindow(tTJSNI_Window *window) {
    return new TVPSDLRuntimeWindowLayer(window);
}

void TVPRemoveWindowLayer(iWindowLayer *layer) {
    if(layer)
        layer->InvalidateClose();
}

tTJSNI_Window *TVPGetActiveWindow() {
    std::lock_guard<std::mutex> lock(gSDLRuntimeWindowMutex);
    return gSDLRuntimeActiveWindow;
}

void TVPSetPostUpdateEvent(void (*callback)()) {
    gSDLRuntimePostUpdate = callback;
}

void TVPSDLRuntimeInvokePostUpdateEvent() {
    if(gSDLRuntimePostUpdate)
        gSDLRuntimePostUpdate();
}

void TVPConsoleLog(const ttstr &message) {
#if defined(__ANDROID__)
    TVPNativeLogInfo("console", message.AsStdString().c_str());
#else
    (void)message;
#endif
}

void TVPConsoleLog(const ttstr &message, bool important) {
    (void)important;
    ::TVPConsoleLog(message);
}

namespace TJS {
void TVPConsoleLog(const ttstr &message) {
    ::TVPConsoleLog(message);
}
} // namespace TJS
