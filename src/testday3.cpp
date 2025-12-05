#include <iostream>
#include <thread>
#include <fstream>

#include "demuxer.h"
#include "queue.h"
#include "videodecoder.h"
#include "audiodecoder.h"
#include "ringbuffer.h"
#include "videofilter.h"
#include "videoencoder.h"
#include "audiofilter.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " input.mp4\n";
        return -1;
    }

    const std::string inputFile = argv[1];

    avformat_network_init();

    // 1. 创建 Demuxer
    Demuxer demuxer(inputFile);
    if (!demuxer.open()) {
        std::cerr << "Failed to open input file\n";
        return -1;
    }

    // 2. 创建队列
    PacketQueue<AVPacket*> audioQueue;
    PacketQueue<AVPacket*> videoQueue;


    // 3. 获取 codecpar
    AVCodecParameters* audioCodecPar = nullptr;
    AVCodecParameters* videoCodecPar = nullptr;

    if (demuxer.getAudioCodecParameters())
        audioCodecPar = demuxer.getAudioCodecParameters();
    if (demuxer.getVideoCodecParameters())
        videoCodecPar = demuxer.getVideoCodecParameters();

    // 4. 创建解码器
    AudioDecoder audioDecoder(audioCodecPar);
    if (!audioDecoder.open()) {
        std::cerr << "Failed to open audio decoder\n";
        return -1;
    }

    VideoDecoder videoDecoder(videoCodecPar);
    if (!videoDecoder.open()) {
        std::cerr << "Failed to open video decoder\n";
        return -1;
    }

    //5. 启动解码线程
    RingBuffer<AVFrame*> audioRingBuf(100);

    std::thread audioThread([&]{
        audioDecoder.decode(audioQueue, [&](AVFrame* frame){
            if (!frame) return;

            // clone 一份，因为 FFmpeg 内部 frame 会复用
            AVFrame* copy = av_frame_clone(frame);
            if (!copy) {
                std::cerr << "Failed to clone frame\n";
                av_frame_unref(frame);
                return;
            }

            // 推入环形缓冲区
            audioRingBuf.push(copy);

            av_frame_unref(frame);
        });

        // 解码结束，通知缓冲区停止
        audioRingBuf.stop();
        std::cout << "Audio decoding finished\n";
    });
    
    AudioFilter afilter;
    AVCodecContext* audioDecCtx = audioDecoder.getCodecContext();
    double speed = 2.0;

    if (!afilter.init(audioDecCtx, speed, AV_SAMPLE_FMT_S16, 0)) {
        std::cerr << "Failed to init AudioFilter\n";
        return -1;
    }

    std::thread audioFilterThread([&]{
        std::ofstream out("filtered_audio.pcm", std::ios::binary);
        if (!out) { std::cerr << "Failed to open file\n"; return; }

        AVFrame* frame = nullptr;
        while (audioRingBuf.pop(frame)) {
            if (!frame) continue;

            afilter.filterFrame(frame, [&](AVFrame* f){
                int sampleSize = av_get_bytes_per_sample((AVSampleFormat)f->format);
                int dataSize = f->nb_samples * sampleSize * f->channels;
                out.write((char*)f->data[0], dataSize);
            });

            av_frame_free(&frame);
        }

        // flush
        afilter.filterFrame(nullptr, [&](AVFrame* f){
            int sampleSize = av_get_bytes_per_sample((AVSampleFormat)f->format);
            int dataSize = f->nb_samples * sampleSize * f->channels;
            out.write((char*)f->data[0], dataSize);
        });

        std::cout << "[AudioFilterThread] finished\n";
    });

    // 6. 启动 Demuxer，填充队列
    demuxer.start(audioQueue, videoQueue);

    // 7. 等待线程结束
    audioThread.join();
    audioFilterThread.join();

    std::cout << "Transcode finished\n";
    return 0;
}
