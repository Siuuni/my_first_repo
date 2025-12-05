#include "audioencoder.h"
#include <iostream>

AudioEncoder::AudioEncoder(AVCodecID codec_id) {
    codec_ = avcodec_find_encoder(codec_id);
    if (!codec_) {
        std::cerr << "AudioEncoder: codec not found\n";
    }
}

AudioEncoder::~AudioEncoder() {
    close();
}

bool AudioEncoder::open(int sample_rate, int channels, AVSampleFormat fmt, int bitrate) {
    if (!codec_) return false;

    codecCtx_ = avcodec_alloc_context3(codec_);
    if (!codecCtx_) return false;

    codecCtx_->sample_rate = sample_rate;
    codecCtx_->channels = channels;
    codecCtx_->channel_layout = av_get_default_channel_layout(channels);
    codecCtx_->sample_fmt = (codec_->sample_fmts) ? codec_->sample_fmts[0] : fmt;
    codecCtx_->bit_rate = bitrate;

    if (avcodec_open2(codecCtx_, codec_, nullptr) < 0) {
        std::cerr << "AudioEncoder: failed to open codec\n";
        return false;
    }
    return true;
}

void AudioEncoder::close() {
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
    }
}

bool AudioEncoder::encode(AVFrame* frame, PacketQueue<AVPacket*>& pktQueue) {
    if (!frame || !codecCtx_) return false;

    int ret = avcodec_send_frame(codecCtx_, frame);
    if (ret < 0) {
        std::cerr << "AudioEncoder: send_frame failed\n";
        return false;
    }

    AVPacket* pkt = av_packet_alloc();
    while ((ret = avcodec_receive_packet(codecCtx_, pkt)) == 0) {
        pktQueue.push(pkt);
        pkt = av_packet_alloc(); // 为下一帧准备
    }
    av_packet_free(&pkt); // flush buffer

    return true;
}

void AudioEncoder::flush(PacketQueue<AVPacket*>& pktQueue) {
    if (!codecCtx_) return;
    avcodec_send_frame(codecCtx_, nullptr);
    AVPacket* pkt = av_packet_alloc();
    while (avcodec_receive_packet(codecCtx_, pkt) == 0) {
        pktQueue.push(pkt);
        pkt = av_packet_alloc();
    }
    av_packet_free(&pkt);
}
