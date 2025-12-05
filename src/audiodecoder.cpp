#include "audiodecoder.h"
#include <iostream>

AudioDecoder::AudioDecoder(AVCodecParameters* codecpar)
    : codecpar_(codecpar) {}

AudioDecoder::~AudioDecoder() {
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
    }
}

bool AudioDecoder::open() {
    if (!codecpar_) return false;

    AVCodec* codec = avcodec_find_decoder(codecpar_->codec_id);
    if (!codec) {
        std::cerr << "Audio codec not found\n";
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx_, codecpar_);
    if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
        std::cerr << "Failed to open audio codec\n";
        return false;
    }

    return true;
}

void AudioDecoder::decode(PacketQueue<AVPacket*>& audioQueue,
                          std::function<void(AVFrame*)> frameCallback) {

    AVPacket* pkt = nullptr;
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        std::cerr << "Failed to alloc frame\n";
        return;
    }

    while (true) {
        pkt = audioQueue.pop();
        if (!pkt) break; // 队列结束

        if (avcodec_send_packet(codecCtx_, pkt) < 0) {
            std::cerr << "Error sending audio packet\n";
            av_packet_free(&pkt);
            continue;
        }

        while (avcodec_receive_frame(codecCtx_, frame) == 0) {
            // 输出原始 frame
            if (frameCallback) frameCallback(frame);

            av_frame_unref(frame); // 清理，方便下一个 frame
        }

        av_packet_free(&pkt);
    }

    av_frame_free(&frame);
}


// void AudioDecoder::decode(PacketQueue<AVPacket*>& audioQueue,
//                           std::function<void(AVFrame*)> frameCallback) {

//     AVPacket* pkt = nullptr;
//     AVFrame* frame = av_frame_alloc();
//     SwrContext* swr = nullptr;
//     bool swrInitialized = false;

//     while (true) {
//         pkt = audioQueue.pop();
//         if (!pkt) break;

//         if (avcodec_send_packet(codecCtx_, pkt) < 0) {
//             std::cerr << "Error sending audio packet\n";
//             av_packet_free(&pkt);
//             continue;
//         }

//         while (avcodec_receive_frame(codecCtx_, frame) == 0) {

//             // 初始化重采样
//             if (!swrInitialized) {
//                 swr = swr_alloc_set_opts(
//                     nullptr,
//                     av_get_default_channel_layout(frame->channels),
//                     AV_SAMPLE_FMT_S16,
//                     frame->sample_rate,

//                     av_get_default_channel_layout(frame->channels),
//                     (AVSampleFormat)frame->format,
//                     frame->sample_rate,
//                     0, nullptr
//                 );
//                 if (!swr || swr_init(swr) < 0) {
//                     std::cerr << "Failed to init swr\n";
//                     return;
//                 }
//                 swrInitialized = true;
//             }

//             // 重采样输出 AVFrame
//             AVFrame* pcmFrame = av_frame_alloc();
//             pcmFrame->channel_layout = av_get_default_channel_layout(frame->channels);
//             pcmFrame->sample_rate = frame->sample_rate;
//             pcmFrame->format = AV_SAMPLE_FMT_S16;
//             pcmFrame->nb_samples = frame->nb_samples;

//             if (av_frame_get_buffer(pcmFrame, 0) < 0) {
//                 std::cerr << "Failed to alloc pcm frame\n";
//                 av_frame_free(&pcmFrame);
//                 return;
//             }

//             // 转换
//             int samples = swr_convert(
//                 swr,
//                 pcmFrame->data,
//                 pcmFrame->nb_samples,
//                 (const uint8_t**)frame->data,
//                 frame->nb_samples
//             );

//             pcmFrame->nb_samples = samples;

//             // 回调给上层（你要 push 到 ringbuffer）
//             frameCallback(pcmFrame);

//             av_frame_unref(frame);
//         }

//         av_packet_free(&pkt);
//     }

//     if (swr) swr_free(&swr);
//     av_frame_free(&frame);
// }

void AudioDecoder::decode(PacketQueue<AVPacket*>& audioQueue,
                          std::function<void(const uint8_t*, int)> pcmCallback) {
    AVPacket* pkt = nullptr;
    AVFrame* frame = av_frame_alloc();
    SwrContext* swr = nullptr; // 重采样/格式转换上下文
    bool swrInitialized = false;

    while (true) {
        pkt = audioQueue.pop();
        if (!pkt) break; // 队列结束

        if (avcodec_send_packet(codecCtx_, pkt) < 0) {
            std::cerr << "Error sending audio packet\n";
            av_packet_free(&pkt);
            continue;
        }

        while (avcodec_receive_frame(codecCtx_, frame) == 0) {

            // 第一次初始化 SwrContext
            if (!swrInitialized) {
                swr = swr_alloc_set_opts(
                    nullptr,
                    av_get_default_channel_layout(frame->channels), // 输出声道布局
                    AV_SAMPLE_FMT_S16,                               // 输出采样格式
                    frame->sample_rate,
                    av_get_default_channel_layout(frame->channels), // 输入声道布局
                    (AVSampleFormat)frame->format,                  // 输入采样格式
                    frame->sample_rate,
                    0, nullptr
                );
                if (!swr || swr_init(swr) < 0) {
                    std::cerr << "Failed to initialize SwrContext\n";
                    return;
                }
                swrInitialized = true;
            }

            // 转换采样
            int dst_nb_samples = av_rescale_rnd(
                swr_get_delay(swr, frame->sample_rate) + frame->nb_samples,
                frame->sample_rate,
                frame->sample_rate,
                AV_ROUND_UP
            );

            uint8_t* out_buf = nullptr;
            av_samples_alloc(&out_buf, nullptr, frame->channels, dst_nb_samples, AV_SAMPLE_FMT_S16, 0);

            int converted_samples = swr_convert(
                swr,
                &out_buf,
                dst_nb_samples,
                (const uint8_t**)frame->data,
                frame->nb_samples
            );

            int out_size = av_samples_get_buffer_size(
                nullptr, frame->channels, converted_samples, AV_SAMPLE_FMT_S16, 1
            );

            // 回调写入文件
            pcmCallback(out_buf, out_size);

            av_freep(&out_buf);
            av_frame_unref(frame);
        }

        av_packet_free(&pkt);
    }

    if (swr) swr_free(&swr);
    av_frame_free(&frame);
}

