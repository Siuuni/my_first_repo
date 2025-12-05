// AudioFilter.cpp
#include "audiofilter.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <cmath>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

AudioFilter::AudioFilter() {}
AudioFilter::~AudioFilter() { close(); }

void AudioFilter::close() {
    if (graph_) avfilter_graph_free(&graph_);
    graph_ = nullptr;
    srcCtx_ = nullptr;
    sinkCtx_ = nullptr;
    initialized_ = false;
}

void AudioFilter::print_av_error(int ret, const char* prefix) {
    if (ret >= 0) return;
    char errbuf[256];
    av_strerror(ret, errbuf, sizeof(errbuf));
    std::cerr << prefix << ": " << errbuf << " (" << ret << ")\n";
}

bool AudioFilter::build_atempo_chain(double speed, std::string &outChain) {
    if (speed <= 0.0) return false;
    if (fabs(speed - 1.0) < 1e-6) { outChain.clear(); return true; }

    std::vector<double> factors;
    double rem = speed;
    while (rem > 2.0 + 1e-9) { factors.push_back(2.0); rem /= 2.0; }
    while (rem < 0.5 - 1e-9) { factors.push_back(0.5); rem /= 0.5; }
    if (fabs(rem - 1.0) > 1e-6) factors.push_back(rem);

    std::ostringstream ss;
    bool first = true;
    for (double f : factors) {
        if (!first) ss << ",";
        ss << "atempo=" << f;
        first = false;
    }
    outChain = ss.str();
    return true;
}

bool AudioFilter::init(AVCodecContext* decCtx, double speed,
                       AVSampleFormat out_sample_fmt,
                       uint64_t out_channel_layout) {
    if (!decCtx) return false;

    close();

    outSampleFmt_ = out_sample_fmt;
    outSampleRate_ = decCtx->sample_rate;
    outChannelLayout_ = out_channel_layout ? out_channel_layout :
                        (decCtx->channel_layout ? decCtx->channel_layout :
                         av_get_default_channel_layout(decCtx->channels));

    std::string atempoChain;
    if (!build_atempo_chain(speed, atempoChain)) {
        std::cerr << "Failed to build atempo chain\n";
        return false;
    }

    graph_ = avfilter_graph_alloc();
    if (!graph_) return false;

    const AVFilter* abuffer = avfilter_get_by_name("abuffer");
    const AVFilter* abuffersink = avfilter_get_by_name("abuffersink");
    if (!abuffer || !abuffersink) return false;

    char args[512];
    snprintf(args, sizeof(args),
             "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%llx",
             decCtx->sample_rate,
             decCtx->sample_rate,
             av_get_sample_fmt_name(decCtx->sample_fmt),
             (unsigned long long)(decCtx->channel_layout ? decCtx->channel_layout :
                                  av_get_default_channel_layout(decCtx->channels)));

    int ret = avfilter_graph_create_filter(&srcCtx_, abuffer, "src", args, nullptr, graph_);
    if (ret < 0) { print_av_error(ret, "create abuffer"); return false; }

    ret = avfilter_graph_create_filter(&sinkCtx_, abuffersink, "sink", nullptr, nullptr, graph_);
    if (ret < 0) { print_av_error(ret, "create abuffersink"); return false; }

    std::string filterDesc = atempoChain.empty() ? "" : atempoChain + ",";
    filterDesc += "aformat=sample_fmts=" + std::string(av_get_sample_fmt_name(outSampleFmt_)) +
                  ":sample_rates=" + std::to_string(outSampleRate_) +
                  ":channel_layouts=" + std::to_string(outChannelLayout_);

    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs  = avfilter_inout_alloc();
    if (!outputs || !inputs) return false;

    outputs->name       = av_strdup("in");
    outputs->filter_ctx = srcCtx_;
    outputs->pad_idx    = 0;
    outputs->next       = nullptr;

    inputs->name        = av_strdup("out");
    inputs->filter_ctx  = sinkCtx_;
    inputs->pad_idx     = 0;
    inputs->next        = nullptr;

    ret = avfilter_graph_parse_ptr(graph_, filterDesc.c_str(), &inputs, &outputs, nullptr);
    if (ret < 0) { print_av_error(ret, "parse graph"); return false; }

    ret = avfilter_graph_config(graph_, nullptr);
    if (ret < 0) { print_av_error(ret, "config graph"); return false; }

    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    initialized_ = true;
    return true;
}

void AudioFilter::filterFrame(AVFrame* frame, std::function<void(AVFrame*)> callback) {
    if (!initialized_ || !graph_ || !srcCtx_ || !sinkCtx_) return;
    if (!frame) return;

    int ret = av_buffersrc_add_frame_flags(srcCtx_, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0) { print_av_error(ret, "av_buffersrc_add_frame_flags"); return; }

    AVFrame* filt = av_frame_alloc();
    while (true) {
        ret = av_buffersink_get_frame(sinkCtx_, filt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) { print_av_error(ret, "buffersink_get_frame"); break; }
        callback(filt);
        av_frame_unref(filt);
    }
    av_frame_free(&filt);
}
