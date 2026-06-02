#pragma once

#include <atomic>
#include <string>

#include "ConfigManager/GlobalConfigManager.h"

enum class tTVPFFmpegDecodeMode {
    Software = 0,
    Hardware = 1,
};

inline std::atomic_bool TVPFFmpegDecodeModeOverrideSet{ false };
inline std::atomic_int TVPFFmpegDecodeModeOverride{
    static_cast<int>(tTVPFFmpegDecodeMode::Software)
};

inline tTVPFFmpegDecodeMode TVPNormalizeFFmpegDecodeMode(int mode) {
    return mode == static_cast<int>(tTVPFFmpegDecodeMode::Hardware)
        ? tTVPFFmpegDecodeMode::Hardware
        : tTVPFFmpegDecodeMode::Software;
}

inline tTVPFFmpegDecodeMode
TVPNormalizeFFmpegDecodeMode(const std::string &mode) {
    if(mode == "hardware" || mode == "hw" || mode == "1")
        return tTVPFFmpegDecodeMode::Hardware;
    return tTVPFFmpegDecodeMode::Software;
}

inline const char *TVPFFmpegDecodeModeName(tTVPFFmpegDecodeMode mode) {
    return mode == tTVPFFmpegDecodeMode::Hardware ? "hardware" : "software";
}

inline int TVPFFmpegDecodeModeValue(tTVPFFmpegDecodeMode mode) {
    return static_cast<int>(mode);
}

inline void TVPSetFFmpegDecodeMode(int mode) {
    TVPFFmpegDecodeModeOverride.store(
        TVPFFmpegDecodeModeValue(TVPNormalizeFFmpegDecodeMode(mode)),
        std::memory_order_relaxed);
    TVPFFmpegDecodeModeOverrideSet.store(true, std::memory_order_release);
}

inline tTVPFFmpegDecodeMode TVPGetFFmpegDecodeMode() {
    if(TVPFFmpegDecodeModeOverrideSet.load(std::memory_order_acquire)) {
        return TVPNormalizeFFmpegDecodeMode(
            TVPFFmpegDecodeModeOverride.load(std::memory_order_relaxed));
    }

    return TVPNormalizeFFmpegDecodeMode(
        GlobalConfigManager::GetInstance()->GetValue<std::string>(
            "ffmpeg_decode_mode", "software"));
}

inline bool TVPPreferFFmpegHardwareDecode() {
    return TVPGetFFmpegDecodeMode() == tTVPFFmpegDecodeMode::Hardware;
}
