#pragma once
#include "queue.h"
#include <functional>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

class VideoDecoder {
public:
    VideoDecoder(AVCodecParameters* codecpar);
    ~VideoDecoder();

    bool open();

    // 从视频包队列解码，回调输出 AVFrame*（YUV）
    void decode(PacketQueue<AVPacket*>& videoQueue,
                std::function<void(AVFrame*)> frameCallback);

                
    AVCodecContext* getCodecContext() const { return codecCtx_; }

private:
    AVCodecContext* codecCtx_ = nullptr;
    AVCodecParameters* codecpar_ = nullptr;
};
