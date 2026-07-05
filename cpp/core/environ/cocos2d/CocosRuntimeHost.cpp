#include "CocosRuntimeHost.h"

#include "MainScene.h"
#include "NativeLog.h"
#include "runtime/RuntimeEngineLoop.h"
#include "runtime/RuntimeHost.h"
#include "sdl/SDLRuntimePresenter.h"

#include <cstdio>

namespace {

class TVPCocosRuntimeHost final : public iTVPRuntimeHost {
public:
    const char *GetHostName() const override { return "cocos2d"; }

    bool StartGame(const TVPRuntimeHostLaunchRequest &request) override {
        TVPMainScene *scene = Scene;
        if(!scene) {
            TVPNativeLogInfo("runtime-host",
                             "cocos host start rejected: scene is null");
            return false;
        }
        return scene->startupFrom(request.gamePath, request.preferenceRoot);
    }

    void RunFrame(float deltaSeconds) override {
        TVPRuntimeRunApplicationFrame(deltaSeconds);
    }

    TVPRuntimeHostFrameMetrics GetFrameMetrics() override {
        TVPMainScene *scene = Scene;
        if(!scene)
            return {};
        return scene->GetRuntimeFrameMetrics();
    }

    void Attach(TVPMainScene *scene) { Scene = scene; }

    void Detach(TVPMainScene *scene) {
        if(Scene == scene)
            Scene = nullptr;
    }

private:
    TVPMainScene *Scene = nullptr;
};

TVPCocosRuntimeHost gCocosRuntimeHost;

void LogRuntimeHostRegistration(const char *action, TVPMainScene *scene) {
#if defined(__ANDROID__)
    char message[160];
    std::snprintf(message, sizeof(message), "cocos runtime host %s scene=%p",
                  action ? action : "", static_cast<void *>(scene));
    TVPNativeLogInfo("runtime-host", message);
#else
    (void)action;
    (void)scene;
#endif
}

} // namespace

void TVPRegisterCocosRuntimeHost(TVPMainScene *scene) {
    gCocosRuntimeHost.Attach(scene);
    TVPSetRuntimeHost(&gCocosRuntimeHost);
    TVPRegisterSDLRuntimePresenter();
    LogRuntimeHostRegistration("registered", scene);
}

void TVPUnregisterCocosRuntimeHost(TVPMainScene *scene) {
    gCocosRuntimeHost.Detach(scene);
    if(TVPGetRuntimeHost() == &gCocosRuntimeHost)
        TVPSetRuntimeHost(nullptr);
    LogRuntimeHostRegistration("unregistered", scene);
}
