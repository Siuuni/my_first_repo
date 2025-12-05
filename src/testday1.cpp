#include <iostream>
#include <thread>
#include <fstream>

#include "demuxer.h"
#include "queue.h"
#include "videodecoder.h"
#include "audiodecoder.h"

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

    // 5. 启动解码线程
    std::thread audioThread([&]{
        std::ofstream ofs("audio.pcm", std::ios::binary);

        audioDecoder.decode(audioQueue, [&](const uint8_t* buf, int size){
            ofs.write((char*)buf, size);
        });

        std::cout << "Audio decoding finished\n";
    });


    std::thread videoThread([&]{
        std::ofstream ofs("video.yuv", std::ios::binary); // 所有视频写入一个文件
        videoDecoder.decode(videoQueue, [&](AVFrame* frame){
            // 写 Y plane
            for (int y = 0; y < frame->height; y++)
                ofs.write((char*)frame->data[0] + y * frame->linesize[0], frame->width);
            // 写 U plane
            for (int y = 0; y < frame->height / 2; y++)
                ofs.write((char*)frame->data[1] + y * frame->linesize[1], frame->width / 2);
            // 写 V plane
            for (int y = 0; y < frame->height / 2; y++)
                ofs.write((char*)frame->data[2] + y * frame->linesize[2], frame->width / 2);
            });
        std::cout << "Video decoding finished\n";
    });


    // 6. 启动 Demuxer，填充队列
    demuxer.start(audioQueue, videoQueue);

    // 7. 等待线程结束
    audioThread.join();
    videoThread.join();

    std::cout << "Transcode finished\n";
    return 0;
}
