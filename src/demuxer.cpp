#include "demuxer.h"
#include <iostream>

Demuxer::Demuxer(const std::string& filename)
    : filename_(filename) {}

Demuxer::~Demuxer() {
    if (fmtCtx_) {
        avformat_close_input(&fmtCtx_);
        fmtCtx_ = nullptr;
    }
}

bool Demuxer::open() {
    if (avformat_open_input(&fmtCtx_, filename_.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "Failed to open input file: " << filename_ << std::endl;
        return false;
    }
    if (avformat_find_stream_info(fmtCtx_, nullptr) < 0) {
        std::cerr << "Failed to find stream info\n";
        return false;
    }

    // 找到音频流和视频流索引
    for (unsigned i = 0; i < fmtCtx_->nb_streams; ++i) {
        if (fmtCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamIndex_ == -1)
            audioStreamIndex_ = i;
        else if (fmtCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStreamIndex_ == -1)
            videoStreamIndex_ = i;
    }

    if (audioStreamIndex_ < 0 && videoStreamIndex_ < 0) {
        std::cerr << "No audio or video stream found\n";
        return false;
    }

     // 设置视频流的 time_base 为 1/24
    if (videoStreamIndex_ >= 0) {
        AVStream* videoStream = fmtCtx_->streams[videoStreamIndex_];
        // 手动设置 time_base 为 1/24 (24 FPS)
        videoStream->time_base = (AVRational){1, 24};
    }

    std::cout << "Audio Stream Index: " << audioStreamIndex_ << ", Video Stream Index: " << videoStreamIndex_ << std::endl;
    return true;
}

void Demuxer::start(PacketQueue<AVPacket*>& audioQueue,
                    PacketQueue<AVPacket*>& videoQueue) {
    AVPacket* pkt = av_packet_alloc();

    while (av_read_frame(fmtCtx_, pkt) >= 0) {
        // 根据流索引放入对应队列
        if (pkt->stream_index == audioStreamIndex_) {
            AVPacket* audioPkt = av_packet_alloc();
            av_packet_ref(audioPkt, pkt);   // 深拷贝包
            audioQueue.push(audioPkt);
        } else if (pkt->stream_index == videoStreamIndex_) {
            AVPacket* videoPkt = av_packet_alloc();
            av_packet_ref(videoPkt, pkt);   // 深拷贝包
            videoQueue.push(videoPkt);
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);

    // 可以向队列发送结束信号（nullptr）
    audioQueue.push(nullptr);
    videoQueue.push(nullptr);

    std::cout << "Demux finished\n";
}

AVCodecParameters* Demuxer::getAudioCodecParameters() const { 
    if (audioStreamIndex_ >= 0) 
        return fmtCtx_->streams[audioStreamIndex_]->codecpar; 
    return nullptr; 
}

AVCodecParameters* Demuxer::getVideoCodecParameters() const { 
    if (videoStreamIndex_ >= 0) 
        return fmtCtx_->streams[videoStreamIndex_]->codecpar; 
    return nullptr; 
}