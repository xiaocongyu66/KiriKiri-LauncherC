#include "RuntimePresenter.h"

#include "NativeLog.h"

#include <atomic>
#include <cstdio>

namespace {

std::atomic<iTVPRuntimePresenter *> gRuntimePresenter{ nullptr };

const char *SafeString(const char *value, const char *fallback) {
    return value && *value ? value : fallback;
}

void LogRuntimePresenterChange(const char *action,
                               iTVPRuntimePresenter *presenter) {
#if defined(__ANDROID__)
    char message[160];
    std::snprintf(message, sizeof(message), "presenter %s name=%s ptr=%p",
                  SafeString(action, "set"),
                  presenter ? SafeString(presenter->GetPresenterName(),
                                         "unknown")
                            : "none",
                  static_cast<void *>(presenter));
    TVPNativeLogInfo("runtime-presenter", message);
#else
    (void)action;
    (void)presenter;
#endif
}

} // namespace

void iTVPRuntimePresenter::SetScreenTakeoverEnabled(
    const TVPRuntimeScreenTakeoverRequest &request) {
    (void)request;
}

bool iTVPRuntimePresenter::IsScreenTakeoverSupported() { return false; }

bool iTVPRuntimePresenter::IsScreenTakeoverEnabled() { return false; }

bool iTVPRuntimePresenter::HasPresentedFrame() { return false; }

bool iTVPRuntimePresenter::PumpScreenPresenter(const char *stage) {
    (void)stage;
    return false;
}

bool iTVPRuntimePresenter::PresentTexture(
    const TVPRuntimeTexturePresentRequest &request) {
    (void)request;
    return false;
}

bool iTVPRuntimePresenter::PresentHostWindowTexture(
    tTJSNI_BaseWindow *window, const TVPRuntimeTexturePresentRequest &request) {
    (void)window;
    (void)request;
    return false;
}

void iTVPRuntimePresenter::RecordOverlayFrame(float deltaSeconds) {
    (void)deltaSeconds;
}

void TVPSetRuntimePresenter(iTVPRuntimePresenter *presenter) {
    iTVPRuntimePresenter *previous =
        gRuntimePresenter.exchange(presenter, std::memory_order_acq_rel);
    if(previous != presenter)
        LogRuntimePresenterChange(presenter ? "registered" : "cleared",
                                  presenter);
}

iTVPRuntimePresenter *TVPGetRuntimePresenter() {
    return gRuntimePresenter.load(std::memory_order_acquire);
}

const char *TVPGetRuntimePresenterName() {
    iTVPRuntimePresenter *presenter = TVPGetRuntimePresenter();
    return presenter ? presenter->GetPresenterName() : "none";
}

void TVPRuntimeSetScreenTakeoverEnabled(
    const TVPRuntimeScreenTakeoverRequest &request) {
    if(iTVPRuntimePresenter *presenter = TVPGetRuntimePresenter())
        presenter->SetScreenTakeoverEnabled(request);
}

bool TVPRuntimeIsScreenTakeoverSupported() {
    iTVPRuntimePresenter *presenter = TVPGetRuntimePresenter();
    return presenter && presenter->IsScreenTakeoverSupported();
}

bool TVPRuntimeIsScreenTakeoverEnabled() {
    iTVPRuntimePresenter *presenter = TVPGetRuntimePresenter();
    return presenter && presenter->IsScreenTakeoverEnabled();
}

bool TVPRuntimeHasPresentedFrame() {
    iTVPRuntimePresenter *presenter = TVPGetRuntimePresenter();
    return presenter && presenter->HasPresentedFrame();
}

bool TVPRuntimePumpScreenPresenter(const char *stage) {
    iTVPRuntimePresenter *presenter = TVPGetRuntimePresenter();
    return presenter && presenter->PumpScreenPresenter(stage);
}

bool TVPRuntimePresentTexture(const TVPRuntimeTexturePresentRequest &request) {
    iTVPRuntimePresenter *presenter = TVPGetRuntimePresenter();
    return presenter && presenter->PresentTexture(request);
}

bool TVPRuntimePresentHostWindowTexture(
    tTJSNI_BaseWindow *window, const TVPRuntimeTexturePresentRequest &request) {
    iTVPRuntimePresenter *presenter = TVPGetRuntimePresenter();
    return presenter && presenter->PresentHostWindowTexture(window, request);
}

void TVPRuntimeRecordOverlayFrame(float deltaSeconds) {
    if(iTVPRuntimePresenter *presenter = TVPGetRuntimePresenter())
        presenter->RecordOverlayFrame(deltaSeconds);
}
