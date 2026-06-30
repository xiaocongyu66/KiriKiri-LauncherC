#pragma once

class iTVPTexture2D;
class tTJSNI_BaseWindow;

struct TVPRuntimeScreenTakeoverRequest {
    bool enabled = false;
    const char *reason = nullptr;
    int frameWidth = 0;
    int frameHeight = 0;
    int sceneWidth = 0;
    int sceneHeight = 0;
};

struct TVPRuntimeTexturePresentRequest {
    iTVPTexture2D *texture = nullptr;
    const char *stage = nullptr;
    int layerWidth = 0;
    int layerHeight = 0;
};

class iTVPRuntimePresenter {
public:
    virtual ~iTVPRuntimePresenter() = default;

    virtual const char *GetPresenterName() const = 0;
    virtual void
    SetScreenTakeoverEnabled(const TVPRuntimeScreenTakeoverRequest &request);
    virtual bool IsScreenTakeoverSupported();
    virtual bool IsScreenTakeoverEnabled();
    virtual bool HasPresentedFrame();
    virtual bool PumpScreenPresenter(const char *stage);
    virtual bool PresentTexture(const TVPRuntimeTexturePresentRequest &request);
    virtual bool
    PresentHostWindowTexture(tTJSNI_BaseWindow *window,
                             const TVPRuntimeTexturePresentRequest &request);
    virtual void RecordOverlayFrame(float deltaSeconds);
};

void TVPSetRuntimePresenter(iTVPRuntimePresenter *presenter);
iTVPRuntimePresenter *TVPGetRuntimePresenter();
const char *TVPGetRuntimePresenterName();
void TVPRuntimeSetScreenTakeoverEnabled(
    const TVPRuntimeScreenTakeoverRequest &request);
bool TVPRuntimeIsScreenTakeoverSupported();
bool TVPRuntimeIsScreenTakeoverEnabled();
bool TVPRuntimeHasPresentedFrame();
bool TVPRuntimePumpScreenPresenter(const char *stage);
bool TVPRuntimePresentTexture(const TVPRuntimeTexturePresentRequest &request);
bool TVPRuntimePresentHostWindowTexture(
    tTJSNI_BaseWindow *window, const TVPRuntimeTexturePresentRequest &request);
void TVPRuntimeRecordOverlayFrame(float deltaSeconds);
