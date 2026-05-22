#include <spdlog/spdlog.h>
#include "AppDelegate.h"

#if defined(__ANDROID__)
#include <spdlog/sinks/android_sink.h>
#else
#include <spdlog/sinks/stdout_color_sinks.h>
#endif

#include "MainScene.h"
#include "Application.h"
#include "Platform.h"
#include "ui/GlobalPreferenceForm.h"
#include "ui/MainFileSelectorForm.h"
#include "ui/extension/UIExtension.h"
#include "ConfigManager/LocaleConfigManager.h"

#if defined(__ANDROID__)
#include <android/log.h>
#define KR2_LAUNCH_LOG(...)                                                    \
    __android_log_print(ANDROID_LOG_INFO, "KR2-Launch", __VA_ARGS__)
#else
#define KR2_LAUNCH_LOG(...) ((void)0)
#endif

static cocos2d::Size designSize(960, 640);
extern std::thread::id TVPMainThreadID;

extern "C" void SDL_SetMainReady();

bool TVPCheckStartupArg();

std::string TVPGetCurrentLanguage();
std::string Android_GetLaunchGamePath();

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

    // Engine logging used to silently drop because no one ever called
    // spdlog::register_logger() for the "core" / "tjs2" names that
    // CsdUIFactory.h, MainScene.cpp etc. resolve via spdlog::get(...).
    // Without registration spdlog::get returns nullptr and every
    // get("core")->info(...) call is a noop, hiding the entire engine
    // event stream that we need to diagnose black-screen / load issues.
    //
    // Wire it up here once: on Android we sink to logcat (visible via
    // adb / 78.log), on desktop we sink to stdout. Level defaults to
    // info; debug builds bump to debug.
    static std::once_flag s_log_init;
    std::call_once(s_log_init, []() {
#if defined(__ANDROID__)
        auto sink = std::make_shared<
            spdlog::sinks::android_sink_mt>("krkr2", true);
#else
        auto sink = std::make_shared<
            spdlog::sinks::stdout_color_sink_mt>();
#endif
        for(const char *name : { "core", "tjs2", "audio", "video" }) {
            if(!spdlog::get(name)) {
                auto logger = std::make_shared<spdlog::logger>(name, sink);
                spdlog::register_logger(logger);
            }
        }
        spdlog::set_default_logger(spdlog::get("core"));
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
    // Set the design resolution
    cocos2d::Size screenSize = glview->getFrameSize();
    if(screenSize.width < screenSize.height) {
        std::swap(screenSize.width, screenSize.height);
    }
    cocos2d::Size ds = designSize;
    ds.height = ds.width * screenSize.height / screenSize.width;
    glview->setDesignResolutionSize(screenSize.width, screenSize.height,
                                    ResolutionPolicy::EXACT_FIT);
#else
    glview->setDesignResolutionSize(designSize.width, designSize.height,
                                    ResolutionPolicy::FIXED_WIDTH);
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
            TVPGlobalPreferenceForm::Initialize();
            // IMPORTANT: TVPCheckStartupArg() registers the per-frame event
            // dispatcher (Android_PushEvents → director scheduler). Without
            // it, every async TJS storage/event call after startupFrom()
            // silently drops, leaving native paths as nil and crashing
            // inside TVPGetPlacedPath(). The original flow always ran this
            // before opening the file selector, so we must run it here too
            // when we shortcut directly into startupFrom().
            bool startupArgHandled = TVPCheckStartupArg();
            std::string gamePath = Android_GetLaunchGamePath();
            KR2_LAUNCH_LOG("Android_GetLaunchGamePath -> '%s' (len=%zu)",
                           gamePath.c_str(), gamePath.size());
            if(!gamePath.empty()) {
                bool ok = TVPMainScene::GetInstance()->startupFrom(gamePath);
                KR2_LAUNCH_LOG("startupFrom('%s') returned %d",
                               gamePath.c_str(), (int)ok);
                if(ok)
                    return;
            }
            KR2_LAUNCH_LOG(
                "falling back to file selector (startupArgHandled=%d)",
                (int)startupArgHandled);
            if(!startupArgHandled) {
                TVPMainScene::GetInstance()->pushUIForm(
                    TVPMainFileSelectorForm::create());
            }
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
