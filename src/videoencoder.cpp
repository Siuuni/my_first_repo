#include "videoencoder.h"
#include <iostream>

VideoEncoder::VideoEncoder(AVCodecID codec_id): codec_(avcodec_find_encoder_by_name("libx264")) {
    //codec_ = avcodec_find_encoder(codec_id);
    if (!codec_) {
        std::cerr << "VideoEncoder: codec not found\n";
    }
}
VideoEncoder::~VideoEncoder() {
    close();
}

bool VideoEncoder::open(int width, int height, AVRational time_base, AVPixelFormat pix_fmt, int fps) {
    if (!codec_) return false;

    codecCtx_ = avcodec_alloc_context3(codec_);
    if (!codecCtx_) return false;

    codecCtx_->width = width;
    codecCtx_->height = height;
    codecCtx_->time_base = time_base;
    codecCtx_->framerate = {fps, 1};

    codecCtx_->pix_fmt = pix_fmt;

    codecCtx_->gop_size = 12;
    codecCtx_->max_b_frames = 2;

    if (codec_->id == AV_CODEC_ID_H264) {
        av_opt_set(codecCtx_->priv_data, "preset", "fast", 0);
    }
    
    int ret = avcodec_open2(codecCtx_, codec_, nullptr);
    if (ret < 0) {
        std::cerr << "Failed to open encoder: \n";
        return false;
    }

    return true;
}

void VideoEncoder::close() {
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
    }
}

bool VideoEncoder::encode(AVFrame* frame, PacketQueue<AVPacket*>& pktQueue) {
    if (!codecCtx_ || !frame) return false;

    int ret = avcodec_send_frame(codecCtx_, frame);
    if (ret < 0) {
        std::cerr << "VideoEncoder: send_frame failed\n";
        return false;
    }

    AVPacket* pkt = av_packet_alloc();
    while ((ret = avcodec_receive_packet(codecCtx_, pkt)) == 0) {
        pktQueue.push(pkt);
        pkt = av_packet_alloc();
    }
    av_packet_free(&pkt);
    return true;
}

void VideoEncoder::flush(PacketQueue<AVPacket*>& pktQueue) {
    if (!codecCtx_) return;

    avcodec_send_frame(codecCtx_, nullptr); // flush
    AVPacket* pkt = av_packet_alloc();
    while (avcodec_receive_packet(codecCtx_, pkt) == 0) {
        pktQueue.push(pkt);
        pkt = av_packet_alloc();
    }
    av_packet_free(&pkt);
}
