#include <iostream>
#include <thread>
#include <fstream>

#include "demuxer.h"
#include "queue.h"
#include "videodecoder.h"
#include "audiodecoder.h"
#include "ringbuffer.h"
#include "videofilter.h"
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


    RingBuffer<AVFrame*> videoRingBuf(30); 
    std::thread videoThread([&]{
        videoDecoder.decode(videoQueue, [&](AVFrame* frame){
            // 必须 clone，因为 FFmpeg 的 decode 会复用 AVFrame
            AVFrame* copy = av_frame_clone(frame);
            if (!copy) {
                std::cerr << "Failed to clone AVFrame\n";
                return;
            }

            // 推入环形缓冲区（生产者）
            videoRingBuf.push(copy);
        });

        // 解码结束，通知 ring buffer 停止
        videoRingBuf.stop();
        std::cout << "Video decoding finished\n";
    });

    VideoFilter vfilter;
    std::thread filterThread([&]{
        std::ofstream ofs("rotate.yuv", std::ios::binary);
        
          AVFrame* firstFrame = nullptr;

        // 阻塞等待第一个解码帧（pop 会阻塞直到有数据或停止）
        if (!videoRingBuf.pop(firstFrame)) {
            // 如果 pop 返回 false，说明 ring buffer 已停止且没有数据
            std::cout << "No video frames to filter (ring buffer stopped)\n";
            return;
        }

        // 现在用解码器的 codec context 初始化 filter（此时解码器参数应已就绪）
        AVCodecContext* decCtx = videoDecoder.getCodecContext();
        if (!decCtx) {
            std::cerr << "Failed to get video codec context for filter init\n";
            // 仍然要释放 firstFrame
            av_frame_free(&firstFrame);
            return;
        }

        if (!vfilter.init(decCtx, 90)) {
            std::cerr << "Failed to init video filter\n";
            av_frame_free(&firstFrame);
            return;
        }

        // 处理第一帧
        vfilter.filterFrame(firstFrame, [&](AVFrame* filtFrame){
            // 写 Y plane
            for (int y = 0; y < filtFrame->height; y++)
                ofs.write((char*)filtFrame->data[0] + y * filtFrame->linesize[0], filtFrame->width);
            // 写 U plane
            for (int y = 0; y < filtFrame->height / 2; y++)
                ofs.write((char*)filtFrame->data[1] + y * filtFrame->linesize[1], filtFrame->width / 2);
            // 写 V plane
            for (int y = 0; y < filtFrame->height / 2; y++)
                ofs.write((char*)filtFrame->data[2] + y * filtFrame->linesize[2], filtFrame->width / 2);
        });
        av_frame_free(&firstFrame);
        
        
        AVFrame* frame = nullptr;
        while (videoRingBuf.pop(frame)) {
            vfilter.filterFrame(frame, [&](AVFrame* filtFrame){
                // std::cout << "Filtered frame: width=" << filtFrame->width
                //     << ", height=" << filtFrame->height
                //     << ", linesize=" << filtFrame->linesize[0] << "\n";
                // 写 YUV
                for (int y = 0; y < filtFrame->height; y++)
                    ofs.write((char*)filtFrame->data[0] + y * filtFrame->linesize[0], filtFrame->width);
                for (int y = 0; y < filtFrame->height / 2; y++)
                    ofs.write((char*)filtFrame->data[1] + y * filtFrame->linesize[1], filtFrame->width / 2);
                for (int y = 0; y < filtFrame->height / 2; y++)
                    ofs.write((char*)filtFrame->data[2] + y * filtFrame->linesize[2], filtFrame->width / 2);
            });

            av_frame_free(&frame);
        }
        // flush filter (ensure all internal buffered frames are output)
        vfilter.filterFrame(nullptr, [&](AVFrame* filtFrame){
            for (int y = 0; y < filtFrame->height; y++)
                ofs.write((char*)filtFrame->data[0] + y * filtFrame->linesize[0], filtFrame->width);
            for (int y = 0; y < filtFrame->height / 2; y++)
                ofs.write((char*)filtFrame->data[1] + y * filtFrame->linesize[1], filtFrame->width / 2);
            for (int y = 0; y < filtFrame->height / 2; y++)
                ofs.write((char*)filtFrame->data[2] + y * filtFrame->linesize[2], filtFrame->width / 2);
        });

        std::cout << "Video filtering finished\n";
    });

    // 6. 启动 Demuxer，填充队列
    demuxer.start(audioQueue, videoQueue);

    // 7. 等待线程结束
    videoThread.join();
    filterThread.join();

    std::cout << "Transcode finished\n";
    return 0;
}
