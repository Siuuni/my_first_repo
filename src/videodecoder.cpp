#include "videodecoder.h"
#include <iostream>

VideoDecoder::VideoDecoder(AVCodecParameters* codecpar)
    : codecpar_(codecpar) {}

VideoDecoder::~VideoDecoder() {
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
    }
}

bool VideoDecoder::open() {
    if (!codecpar_) return false;

    AVCodec* codec = avcodec_find_decoder(codecpar_->codec_id);
    if (!codec) {
        std::cerr << "Video codec not found\n";
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx_, codecpar_);
    
    codecCtx_->time_base = (AVRational){1, 24};

    if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
        std::cerr << "Failed to open video codec\n";
        return false;
    }
    return true;
}

void VideoDecoder::decode(PacketQueue<AVPacket*>& videoQueue,
                          std::function<void(AVFrame*)> frameCallback) {
    AVPacket* pkt = nullptr;
    AVFrame* frame = av_frame_alloc();

    while (true) {
        pkt = videoQueue.pop();
        if (!pkt) break; // 队列结束

        if (avcodec_send_packet(codecCtx_, pkt) < 0) {
            std::cerr << "Error sending video packet\n";
            av_packet_free(&pkt);
            continue;
        }

        while (avcodec_receive_frame(codecCtx_, frame) == 0) {
            
            /*static bool printed = false;
            if (!printed) {
                std::cout << "Video Decoded Frame Info:\n";
                std::cout << "  width  = " << frame->width  << "\n";
                std::cout << "  height = " << frame->height << "\n";
                std::cout << "  pix_fmt= " << frame->format << "\n";
                printed = true;
            }*/
            
            frameCallback(frame);
            av_frame_unref(frame);
        }

        av_packet_free(&pkt);
    }

    av_frame_free(&frame);
}
