#pragma once
#include <string>
#include "queue.h"   // 你的线程安全队列模板
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

// Demuxer: 只负责解封装，将音视频包放入队列
class Demuxer {
public:
    Demuxer(const std::string& filename);
    ~Demuxer();

    // 打开文件，找到音视频流
    bool open();

    // 将 AVPacket 推入音频队列和视频队列
    void start(PacketQueue<AVPacket*>& audioQueue,
               PacketQueue<AVPacket*>& videoQueue);
    
    AVCodecParameters* getAudioCodecParameters() const;
    AVCodecParameters* getVideoCodecParameters() const;
private:
    std::string filename_;
    AVFormatContext* fmtCtx_ = nullptr;
    int audioStreamIndex_ = -1;
    int videoStreamIndex_ = -1;
};
