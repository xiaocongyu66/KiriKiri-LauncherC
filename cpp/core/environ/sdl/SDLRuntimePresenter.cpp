#include "SDLRuntimePresenter.h"

#include "SDLGameManager.h"
#include "runtime/RuntimePresenter.h"

namespace {

class TVPSDLRuntimePresenter final : public iTVPRuntimePresenter {
public:
    const char *GetPresenterName() const override { return "sdl3"; }

    void SetScreenTakeoverEnabled(
        const TVPRuntimeScreenTakeoverRequest &request) override {
        TVPSDLSetScreenTakeoverEnabled(
            request.enabled, request.reason, request.frameWidth,
            request.frameHeight, request.sceneWidth, request.sceneHeight);
    }

    bool IsScreenTakeoverSupported() override {
        return TVPSDLIsScreenTakeoverSupported();
    }

    bool IsScreenTakeoverEnabled() override {
        return TVPSDLIsScreenTakeoverEnabled();
    }

    bool HasPresentedFrame() override {
        return TVPSDLHasScreenPresenterPresented();
    }

    bool PumpScreenPresenter(const char *stage) override {
        return TVPSDLPumpScreenPresenter(stage);
    }

    bool PresentTexture(
        const TVPRuntimeTexturePresentRequest &request) override {
        return TVPSDLTryPresentTexture(request.texture, request.stage,
                                       request.layerWidth,
                                       request.layerHeight);
    }

    bool PresentHostWindowTexture(
        tTJSNI_BaseWindow *window,
        const TVPRuntimeTexturePresentRequest &request) override {
        return TVPSDLPresentHostWindowTexture(
            window, request.texture, request.stage, request.layerWidth,
            request.layerHeight);
    }

    void RecordOverlayFrame(float deltaSeconds) override {
        TVPSDLRecordRenderOverlayFrame(deltaSeconds);
    }
};

TVPSDLRuntimePresenter gSDLRuntimePresenter;

} // namespace

void TVPRegisterSDLRuntimePresenter() {
    TVPSetRuntimePresenter(&gSDLRuntimePresenter);
}

void TVPUnregisterSDLRuntimePresenter() {
    if(TVPGetRuntimePresenter() == &gSDLRuntimePresenter)
        TVPSetRuntimePresenter(nullptr);
}
