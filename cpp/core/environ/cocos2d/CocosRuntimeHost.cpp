#include "CocosRuntimeHost.h"

#include "Application.h"
#include "MainScene.h"
#include "NativeLog.h"
#include "runtime/RuntimeHost.h"

#include "base/CCDirector.h"

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
        (void)deltaSeconds;
        ::Application->Run();
    }

    TVPRuntimeHostFrameMetrics GetFrameMetrics() override {
        TVPRuntimeHostFrameMetrics metrics;
        TVPMainScene *scene = Scene;
        if(!scene)
            return metrics;

        auto glview = cocos2d::Director::getInstance()->getOpenGLView();
        cocos2d::Size sceneSize = scene->getGameNodeSize();
        cocos2d::Size frameSize = glview ? glview->getFrameSize() : sceneSize;
        metrics.frameWidth = static_cast<int>(frameSize.width);
        metrics.frameHeight = static_cast<int>(frameSize.height);
        metrics.sceneWidth = static_cast<int>(sceneSize.width);
        metrics.sceneHeight = static_cast<int>(sceneSize.height);
        metrics.scale = sceneSize.height > 0.0f
            ? frameSize.height / sceneSize.height
            : 1.0f;
        if(metrics.scale <= 0.0f)
            metrics.scale = 1.0f;
        return metrics;
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
    LogRuntimeHostRegistration("registered", scene);
}

void TVPUnregisterCocosRuntimeHost(TVPMainScene *scene) {
    gCocosRuntimeHost.Detach(scene);
    if(TVPGetRuntimeHost() == &gCocosRuntimeHost)
        TVPSetRuntimeHost(nullptr);
    LogRuntimeHostRegistration("unregistered", scene);
}
