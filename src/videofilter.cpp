#include "videofilter.h"
#include <iostream>

VideoFilter::VideoFilter() {}
VideoFilter::~VideoFilter() {
    if (filterGraph_) {
        avfilter_graph_free(&filterGraph_);
    }
}

bool VideoFilter::init(AVCodecContext* decCtx, int angle) {
    rotateAngle_ = angle;
    return initFilterGraph(decCtx, angle);
}

bool VideoFilter::initFilterGraph(AVCodecContext* decCtx, int angle) {
    if (!decCtx || decCtx->width <=0 || decCtx->height <=0) {
        std::cerr << "Invalid codec context: width/height not set\n";
        return false;
    }

    AVRational tb = decCtx->time_base;
    if (tb.num <=0 || tb.den <=0) tb = {1,25}; // 默认25fps

    AVRational sar = decCtx->sample_aspect_ratio;
    if (sar.num <=0 || sar.den <=0) sar = {1,1};

    char args[512];
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             decCtx->width, decCtx->height, decCtx->pix_fmt,
             tb.num, tb.den,
             sar.num, sar.den);

    const AVFilter* buffersrc  = avfilter_get_by_name("buffer");
    const AVFilter* buffersink = avfilter_get_by_name("buffersink");
    filterGraph_ = avfilter_graph_alloc();
    if (!filterGraph_) return false;

    int ret = avfilter_graph_create_filter(&buffersrcCtx_, buffersrc, "src",
                                           args, nullptr, filterGraph_);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "Failed to create buffer source: " << errbuf << "\n";
        return false;
    }

    ret = avfilter_graph_create_filter(&buffersinkCtx_, buffersink, "sink",
                                       nullptr, nullptr, filterGraph_);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "Failed to create buffer sink: " << errbuf << "\n";
        return false;
    }

    // rotate filter
    const AVFilter* rotateFilter = avfilter_get_by_name("rotate");
    AVFilterContext* rotateCtx = nullptr;

    double rad = 0.0;
    switch(angle) {
        case 90:  rad = M_PI/2; break;
        case 180: rad = M_PI; break;
        case 270: rad = M_PI*3/2; break;
        default:  rad = 0.0; break;
    }

    char rotateArg[64];
    snprintf(rotateArg, sizeof(rotateArg), "angle=%f", rad);

    ret = avfilter_graph_create_filter(&rotateCtx, rotateFilter, "rotate",
                                       rotateArg, nullptr, filterGraph_);
    if (ret < 0) {
        char errbuf[128]; av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "Failed to create rotate filter: " << errbuf << "\n";
        return false;
    }

    if ((ret = avfilter_link(buffersrcCtx_,0,rotateCtx,0)) < 0 ||
        (ret = avfilter_link(rotateCtx,0,buffersinkCtx_,0)) < 0) {
        std::cerr << "Failed to link filters\n";
        return false;
    }

    if ((ret = avfilter_graph_config(filterGraph_, nullptr)) <0) {
        char errbuf[128]; av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "Failed to configure filter graph: " << errbuf << "\n";
        return false;
    }

    return true;
}


void VideoFilter::filterFrame(AVFrame* frame,
                              std::function<void(AVFrame*)> callback) {
    if (!filterGraph_) return;

    int ret = av_buffersrc_add_frame(buffersrcCtx_, frame);
    if (ret < 0) return;

    AVFrame* filtFrame = av_frame_alloc();

    while (av_buffersink_get_frame(buffersinkCtx_, filtFrame) >= 0) {
        callback(filtFrame);
        av_frame_unref(filtFrame);
    }

    av_frame_free(&filtFrame);
}
