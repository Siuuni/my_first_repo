#pragma once
#include "queue.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
}

class AudioEncoder {
public:
    AudioEncoder(AVCodecID codec_id = AV_CODEC_ID_AC3);
    ~AudioEncoder();

    bool open(int sample_rate, int channels, AVSampleFormat fmt, int bitrate = 192000);
    void close();

    // 将 AVFrame 编码成 AVPacket 并 push 到队列
    bool encode(AVFrame* frame, PacketQueue<AVPacket*>& pktQueue);

    // flush 编码器
    void flush(PacketQueue<AVPacket*>& pktQueue);

    AVCodecContext* getCodecContext() const { return codecCtx_; }

private:
    AVCodec* codec_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
};
