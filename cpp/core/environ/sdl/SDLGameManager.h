#pragma once

#include <functional>
#include <string>

struct TVPSDLGameLaunchCallbacks {
    std::function<void()> initializePreferences;
    std::function<bool(const std::string &path, const std::string &gameDir)>
        startupFrom;
    std::function<void()> showFileSelector;
    std::function<void(const std::string &message)> log;
};

enum class TVPSDLGameLaunchResult {
    Started,
    StartupArgHandled,
    FileSelectorShown,
    NoFallback,
};

TVPSDLGameLaunchResult
TVPSDLRunGameLaunch(const TVPSDLGameLaunchCallbacks &callbacks);
