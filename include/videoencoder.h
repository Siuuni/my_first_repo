#pragma once
#include "queue.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
}

class VideoEncoder {
public:
    VideoEncoder(AVCodecID codec_id = AV_CODEC_ID_H264);
    ~VideoEncoder();

    // 初始化编码器，必须在调用 encodeFrame 前
    bool open(int width, int height, AVRational time_base, AVPixelFormat pix_fmt, int fps);
    void close();

    // 将 AVFrame 编码成 AVPacket 并 push 到队列
    bool encode(AVFrame* frame, PacketQueue<AVPacket*>& pktQueue);

    // flush 编码器，处理剩余帧
    void flush(PacketQueue<AVPacket*>& pktQueue);

    AVCodecContext* getCodecContext() const { return codecCtx_; }
private:
    AVCodec* codec_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
};
