#pragma once
#include "queue.h"
#include <functional>
extern "C" {
#include <libswresample/swresample.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

class AudioDecoder {
public:
    AudioDecoder(AVCodecParameters* codecpar);
    ~AudioDecoder();

    bool open();

    // 从音频包队列解码，回调输出 AVFrame*（PCM）
    void decode(PacketQueue<AVPacket*>& audioQueue,
                          std::function<void(AVFrame*)> frameCallback);

    void decode(PacketQueue<AVPacket*>& audioQueue,
                          std::function<void(const uint8_t*, int)> pcmCallback);
    
    AVCodecContext* getCodecContext() const { return codecCtx_; }

private:
    AVCodecContext* codecCtx_ = nullptr;
    AVCodecParameters* codecpar_ = nullptr;
};
