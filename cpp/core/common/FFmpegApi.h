#pragma once

#ifdef __cplusplus
#include <cstdint>

extern "C" {
#else
#include <stdint.h>
#endif
#include <libavcodec/avcodec.h>
#include <libavcodec/codec_par.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
inline int TVPFFmpegFrameChannels(const AVFrame *frame) {
    if(!frame)
        return 0;
    return frame->ch_layout.nb_channels;
}

inline int TVPFFmpegContextChannels(const AVCodecContext *ctx) {
    if(!ctx)
        return 0;
    return ctx->ch_layout.nb_channels;
}

inline int TVPFFmpegCodecParChannels(const AVCodecParameters *par) {
    if(!par)
        return 0;
    return par->ch_layout.nb_channels;
}

inline uint64_t TVPFFmpegChannelLayoutMask(const AVChannelLayout *layout) {
    if(!layout || layout->nb_channels <= 0)
        return 0;
    return av_channel_layout_subset(layout, UINT64_MAX);
}

inline uint64_t TVPFFmpegContextChannelLayout(const AVCodecContext *ctx) {
    if(!ctx)
        return 0;
    uint64_t layout = TVPFFmpegChannelLayoutMask(&ctx->ch_layout);
    if(layout)
        return layout;
    return 0;
}

inline uint64_t TVPFFmpegCodecParChannelLayout(const AVCodecParameters *par) {
    if(!par)
        return 0;
    uint64_t layout = TVPFFmpegChannelLayoutMask(&par->ch_layout);
    if(layout)
        return layout;
    return 0;
}

inline int64_t TVPFFmpegFrameBestEffortTimestamp(const AVFrame *frame) {
    return frame ? frame->best_effort_timestamp : AV_NOPTS_VALUE;
}
#endif
