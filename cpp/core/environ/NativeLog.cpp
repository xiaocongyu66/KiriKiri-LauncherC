#include "NativeLog.h"

#include <cstdarg>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#if defined(__ANDROID__)
#include <android/log.h>
#include <spdlog/sinks/android_sink.h>
#else
#include <spdlog/sinks/stdout_color_sinks.h>
#endif

namespace {
    std::mutex gLogMutex;
    bool gLogInitialized = false;
    std::string gLogFilePath;

    constexpr const char *kLoggerNames[] = {
        "core", "tjs2", "plugin", "audio", "video",
    };

    spdlog::sink_ptr CreatePlatformSink() {
#if defined(__ANDROID__)
        auto sink = std::make_shared<spdlog::sinks::android_sink_mt>("krkr2",
                                                                     true);
        sink->set_pattern("%v");
        return sink;
#else
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        sink->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
        return sink;
#endif
    }

    spdlog::sink_ptr CreateFileSink(const std::string &path) {
        if(path.empty())
            return nullptr;
        std::error_code ec;
        const auto parent = std::filesystem::path(path).parent_path();
        if(!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
        }
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            path, true);
        sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
        return sink;
    }

    std::vector<spdlog::sink_ptr> BuildSinks(bool fileLoggingEnabled,
                                             const std::string &logFilePath) {
        std::vector<spdlog::sink_ptr> sinks;
        sinks.emplace_back(CreatePlatformSink());
        if(fileLoggingEnabled && !logFilePath.empty()) {
            try {
                if(auto fileSink = CreateFileSink(logFilePath)) {
                    sinks.emplace_back(std::move(fileSink));
                }
            } catch(const std::exception &e) {
#if defined(__ANDROID__)
                __android_log_print(ANDROID_LOG_WARN, "krkr2",
                                    "failed to create file log sink: %s",
                                    e.what());
#else
                std::fprintf(stderr, "failed to create file log sink: %s\n",
                             e.what());
#endif
            }
        }
        return sinks;
    }

    void ApplySinksLocked(const std::vector<spdlog::sink_ptr> &sinks) {
        for(const auto *name : kLoggerNames) {
            auto logger = spdlog::get(name);
            if(!logger) {
                logger = std::make_shared<spdlog::logger>(
                    name, sinks.begin(), sinks.end());
                spdlog::register_logger(logger);
            } else {
                auto &loggerSinks = logger->sinks();
                loggerSinks.assign(sinks.begin(), sinks.end());
            }
            logger->flush_on(spdlog::level::info);
        }
        if(auto core = spdlog::get("core")) {
            spdlog::set_default_logger(core);
        }
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::info);
        gLogInitialized = true;
    }

    void LogToPlatform(const char *tag, const char *message) {
#if defined(__ANDROID__)
        __android_log_print(ANDROID_LOG_INFO, tag ? tag : "krkr2", "%s",
                            message ? message : "");
#else
        std::fprintf(stderr, "[%s] %s\n", tag ? tag : "krkr2",
                     message ? message : "");
#endif
    }
} // namespace

void TVPInitializeNativeLogging() {
    std::lock_guard<std::mutex> lock(gLogMutex);
    if(gLogInitialized)
        return;
    gLogFilePath.clear();
    auto sinks = BuildSinks(false, gLogFilePath);
    ApplySinksLocked(sinks);
}

void TVPConfigureNativeLogging(bool fileLoggingEnabled,
                               const std::string &logFilePath) {
    std::lock_guard<std::mutex> lock(gLogMutex);
    gLogFilePath = fileLoggingEnabled ? logFilePath : std::string();
    auto sinks = BuildSinks(fileLoggingEnabled, gLogFilePath);
    ApplySinksLocked(sinks);
    try {
        spdlog::info("native file logging enabled={} path={}",
                     fileLoggingEnabled ? 1 : 0, gLogFilePath);
    } catch(...) {
    }
}

void TVPNativeLogInfo(const char *tag, const char *message) {
    try {
        if(auto logger = spdlog::get("core")) {
            logger->info("[{}] {}", tag ? tag : "native",
                         message ? message : "");
            return;
        }
    } catch(...) {
    }
    LogToPlatform(tag, message);
}

extern "C" void KR2RenderProbeWriteF(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    TVPNativeLogInfo("render", buf);
}
