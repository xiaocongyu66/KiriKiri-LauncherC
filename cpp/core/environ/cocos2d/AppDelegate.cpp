#include <spdlog/spdlog.h>
#include "AppDelegate.h"

#include "MainScene.h"
#include "Application.h"
#include "Platform.h"
#include "ui/GlobalPreferenceForm.h"
#include "ui/MainFileSelectorForm.h"
#include "ui/extension/UIExtension.h"
#include "ConfigManager/LocaleConfigManager.h"
#include "NativeLog.h"
#include "sdl/SDLGameManager.h"
#include "sdl/SDLUIManager.h"

#include <SDL3/SDL_main.h>

#include <cstdio>

#if defined(__ANDROID__)
#define KR2_LAUNCH_LOG(...)                                                    \
    do {                                                                       \
        char kr2LaunchLogBuf[1024];                                            \
        std::snprintf(kr2LaunchLogBuf, sizeof(kr2LaunchLogBuf), __VA_ARGS__);  \
        TVPNativeLogInfo("launch", kr2LaunchLogBuf);                           \
    } while(0)
#else
#define KR2_LAUNCH_LOG(...) ((void)0)
#endif

static cocos2d::Size designSize(960, 640);
static constexpr float TVPLegacyMobileDesignWidth = 2048.0f;
extern std::thread::id TVPMainThreadID;

std::string TVPGetCurrentLanguage();

void TVPAppDelegate::applicationWillEnterForeground() {
    ::Application->OnActivate();
    cocos2d::Director::getInstance()->startAnimation();
}

void TVPAppDelegate::applicationDidEnterBackground() {
    ::Application->OnDeactivate();
    cocos2d::Director::getInstance()->stopAnimation();
}

bool TVPAppDelegate::applicationDidFinishLaunching() {
    SDL_SetMainReady();
    TVPMainThreadID = std::this_thread::get_id();

    static std::once_flag s_log_init;
    std::call_once(s_log_init, []() {
        TVPInitializeNativeLogging();
#if defined(_DEBUG) || defined(TVP_DEBUG)
        spdlog::set_level(spdlog::level::debug);
#else
        spdlog::set_level(spdlog::level::info);
#endif
    });

    spdlog::debug("App Finish Launching");
    // initialize director
    auto director = cocos2d::Director::getInstance();
    auto glview = director->getOpenGLView();
    if(!glview) {
        glview = cocos2d::GLViewImpl::create("krkr2");
        director->setOpenGLView(glview);
#if CC_TARGET_PLATFORM == CC_PLATFORM_WIN32
        HWND hwnd = glview->getWin32Window();
        if(hwnd) {
            // 添加可调节边框和最大化按钮
            LONG style = GetWindowLong(hwnd, GWL_STYLE);
            style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
            SetWindowLong(hwnd, GWL_STYLE, style);
        }
#endif
    }

#if (CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID ||                              \
     CC_TARGET_PLATFORM == CC_PLATFORM_IOS)
    cocos2d::Size frameSize = glview->getFrameSize();
    if(frameSize.width < frameSize.height) {
        std::swap(frameSize.width, frameSize.height);
    }
    if(frameSize.width <= 0.0f || frameSize.height <= 0.0f) {
        frameSize = designSize;
    }
    cocos2d::Size mobileDesignSize(
        TVPLegacyMobileDesignWidth,
        TVPLegacyMobileDesignWidth * frameSize.height / frameSize.width);
    glview->setDesignResolutionSize(mobileDesignSize.width,
                                    mobileDesignSize.height,
                                    ResolutionPolicy::EXACT_FIT);
    TVPSDLUIRecordViewport(static_cast<int>(frameSize.width),
                           static_cast<int>(frameSize.height),
                           static_cast<int>(mobileDesignSize.width),
                           static_cast<int>(mobileDesignSize.height),
                           mobileDesignSize.height > 0.0f
                               ? frameSize.height / mobileDesignSize.height
                               : 1.0f);
    KR2_LAUNCH_LOG(
        "design resolution mode=legacy-2048 physical=%.0fx%.0f "
        "virtual=%.0fx%.0f policy=EXACT_FIT",
        frameSize.width, frameSize.height, mobileDesignSize.width,
        mobileDesignSize.height);
#else
    glview->setDesignResolutionSize(designSize.width, designSize.height,
                                    ResolutionPolicy::FIXED_WIDTH);
    TVPSDLUIRecordViewport(static_cast<int>(designSize.width),
                           static_cast<int>(designSize.height),
                           static_cast<int>(designSize.width),
                           static_cast<int>(designSize.height), 1.0f);
#endif

    std::vector<std::string> searchPath;

    // In this demo, we select resource according to the frame's
    // height. If the resource size is different from design
    // resolution size, you need to set contentScaleFactor. We use the
    // ratio of resource's height to the height of design resolution,
    // this can make sure that the resource's height could fit for the
    // height of design resolution.
    searchPath.emplace_back("res");

    // set searching path
    cocos2d::FileUtils::getInstance()->setSearchPaths(searchPath);
    TVPSDLUIRegisterLegacyCocosStudioAssets("ui/cocos-studio", ".");

    // turn on display FPS
    director->setDisplayStats(false);

    // set FPS. the default value is 1.0/60 if you don't call this
    director->setAnimationInterval(1.0f / 60);

    TVPInitUIExtension();

    // initialize something
    LocaleConfigManager::GetInstance()->Initialize(TVPGetCurrentLanguage());
    // create a scene. it's an autorelease object
    TVPMainScene *scene = TVPMainScene::CreateInstance();

    // run
    director->runWithScene(scene);

    scene->scheduleOnce(
        [](float dt) {
            TVPMainScene::GetInstance()->unschedule("launch");
            TVPSDLGameLaunchCallbacks launchCallbacks;
            launchCallbacks.initializePreferences = []() {
                TVPGlobalPreferenceForm::Initialize();
            };
            launchCallbacks.startupFrom = [](const std::string &gamePath,
                                             const std::string &gameDir) {
                bool ok =
                    TVPMainScene::GetInstance()->startupFrom(gamePath, gameDir);
                KR2_LAUNCH_LOG("startupFrom('%s') returned %d",
                               gamePath.c_str(), (int)ok);
                return ok;
            };
            launchCallbacks.showFileSelector = []() {
                TVPMainScene::GetInstance()->pushUIForm(
                    TVPMainFileSelectorForm::create());
            };
            launchCallbacks.log = [](const std::string &message) {
                KR2_LAUNCH_LOG("%s", message.c_str());
            };
            TVPSDLRunGameLaunch(launchCallbacks);
        },
        0, "launch");

    return true;
}

void TVPAppDelegate::initGLContextAttrs() {
    GLContextAttrs glContextAttrs = { 8, 8, 8, 8, 24, 8 };
    cocos2d::GLView::setGLContextAttrs(glContextAttrs);
}


void TVPOpenPatchLibUrl() {
    cocos2d::Application::getInstance()->openURL(
        "https://zeas2.github.io/Kirikiroid2_patch/patch");
}
