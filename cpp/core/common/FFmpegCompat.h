#pragma once

#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec_par.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/version.h>
}

#ifndef FF_INPUT_BUFFER_PADDING_SIZE
#ifdef AV_INPUT_BUFFER_PADDING_SIZE
#define FF_INPUT_BUFFER_PADDING_SIZE AV_INPUT_BUFFER_PADDING_SIZE
#endif
#endif

#ifndef CODEC_CAP_DR1
#ifdef AV_CODEC_CAP_DR1
#define CODEC_CAP_DR1 AV_CODEC_CAP_DR1
#endif
#endif

#ifndef CODEC_CAP_TRUNCATED
#ifdef AV_CODEC_CAP_TRUNCATED
#define CODEC_CAP_TRUNCATED AV_CODEC_CAP_TRUNCATED
#endif
#endif

#if !defined(CODEC_FLAG_EMU_EDGE) && defined(AV_CODEC_FLAG_EMU_EDGE)
#define CODEC_FLAG_EMU_EDGE AV_CODEC_FLAG_EMU_EDGE
#endif

#if !defined(CODEC_FLAG_TRUNCATED) && defined(AV_CODEC_FLAG_TRUNCATED)
#define CODEC_FLAG_TRUNCATED AV_CODEC_FLAG_TRUNCATED
#endif

#ifndef FF_QSCALE_TYPE_MPEG1
#define FF_QSCALE_TYPE_MPEG1 0
#endif

#ifndef FF_QSCALE_TYPE_MPEG2
#define FF_QSCALE_TYPE_MPEG2 1
#endif

#ifndef FF_QSCALE_TYPE_H264
#define FF_QSCALE_TYPE_H264 2
#endif

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
