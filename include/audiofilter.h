#pragma once
#include <functional>
#include <string>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
}

class AudioFilter {
public:
    AudioFilter();
    ~AudioFilter();

    // 禁止拷贝
    AudioFilter(const AudioFilter&) = delete;
    AudioFilter& operator=(const AudioFilter&) = delete;

    // 初始化 filter graph
    // decCtx: 解码器上下文（用于读取 sample_rate / channels / sample_fmt / channel_layout）
    // speed: 倍速，例如 0.5, 1.0, 2.0, 3.0
    // out_sample_fmt: 期望的输出采样格式（例如 AV_SAMPLE_FMT_S16 or AV_SAMPLE_FMT_FLTP）
    // out_channel_layout: 期望的输出声道布局（0 表示使用 decCtx 的默认）
    bool init(AVCodecContext* decCtx, double speed,
              AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16,
              uint64_t out_channel_layout = 0);

    // 对一帧音频做过滤，回调会被传入处理后的 AVFrame*（caller 负责 av_frame_free）
    void filterFrame(AVFrame* frame, std::function<void(AVFrame*)> callback);

    // 释放/重置
    void close();

private:
    bool build_atempo_chain(double speed, std::string &outChain);
    void print_av_error(int ret, const char* prefix);

private:
    AVFilterGraph* graph_ = nullptr;
    AVFilterContext* srcCtx_ = nullptr;
    AVFilterContext* sinkCtx_ = nullptr;

    // desired output
    AVSampleFormat outSampleFmt_ = AV_SAMPLE_FMT_S16;
    uint64_t outChannelLayout_ = 0;
    int outSampleRate_ = 0;

    bool initialized_ = false;
};
