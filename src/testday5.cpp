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
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

void dumpVideoRingBuf(RingBuffer<AVFrame*>& buf, const std::string& filename)
{
    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs) {
        std::cerr << "Failed to open output file: " << filename << "\n";
        return;
    }

    AVFrame* frame = nullptr;

    // 直接循环 pop，pop 返回 false 表示缓冲区已停止且空
    while (buf.pop(frame)) {
        if (!frame) continue;

        if (frame->format != AV_PIX_FMT_YUV420P) {
            std::cerr << "Warning: frame format = " << frame->format 
                      << ", not YUV420P, skipping\n";
            av_frame_free(&frame);
            continue;
        }

        int w = frame->width;
        int h = frame->height;

        // 写 Y
        for (int i = 0; i < h; ++i)
            ofs.write(reinterpret_cast<const char*>(frame->data[0] + i * frame->linesize[0]), w);
        // 写 U
        for (int i = 0; i < h / 2; ++i)
            ofs.write(reinterpret_cast<const char*>(frame->data[1] + i * frame->linesize[1]), w / 2);
        // 写 V
        for (int i = 0; i < h / 2; ++i)
            ofs.write(reinterpret_cast<const char*>(frame->data[2] + i * frame->linesize[2]), w / 2);

        av_frame_unref(frame);
        av_frame_free(&frame);
    }

    ofs.close();
    std::cout << "Dump finished: " << filename << "\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " input.mp4\n";
        return -1;
    }

    const std::string inputFile = argv[1];
    //av_log_set_level(AV_LOG_DEBUG);
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
    PacketQueue<AVPacket*> videoEncoderQueue;

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

            if (!frame || !frame->data[0]) return;

            // clone/ref 都可以，这里使用 ref 与音频保持一致
            AVFrame* copy = av_frame_alloc();
            if (!copy) return;

            if (av_frame_ref(copy, frame) < 0) {
                av_frame_free(&copy);
                return;
            }

            videoRingBuf.push(copy);  // 放入环形缓冲区，原始帧暂时不释放
        });

        videoRingBuf.stop();
        std::cout << "Video decoding finished\n";
    });

    VideoFilter vfilter;
    AVCodecContext* videoDecCtx = videoDecoder.getCodecContext();
    int rotateAngle = 90; 
    if (!vfilter.init(videoDecCtx, rotateAngle)) {
        std::cerr << "Failed to init VideoFilter\n";
    }

    VideoEncoder videoEncoder;
    if (!videoEncoder.open(
            videoDecCtx->width,
            videoDecCtx->height,
            videoDecCtx->time_base, 
            videoDecCtx->pix_fmt, 
            videoDecCtx->framerate.num // 假设视频的帧率是 24fps
        )) {
        std::cerr << "Failed to open VideoEncoder\n";
        return -1;
    }

   std::thread videoEncodeThread([&]{
    AVFrame* frame = nullptr;

    // 创建输出文件上下文
    AVFormatContext* outputFmtCtx = nullptr;
    AVOutputFormat* outputFmt = av_guess_format("mp4", nullptr, nullptr);
    if (!outputFmt) {
        std::cerr << "Failed to guess output format\n";
        return;
    }

    // 为输出文件分配 AVFormatContext
    if (avformat_alloc_output_context2(&outputFmtCtx, outputFmt, nullptr, "output.mp4") < 0) {
        std::cerr << "Failed to allocate output context\n";
        return;
    }

    // 创建视频流
    AVStream* outVideoStream = avformat_new_stream(outputFmtCtx, nullptr);
    if (!outVideoStream) {
        std::cerr << "Failed to create video stream\n";
        return;
    }

    // 获取 AVCodecParameters 并创建 AVCodecContext
    AVCodecParameters* codecpar = outVideoStream->codecpar;
    AVCodecContext* outVideoCodecCtx = avcodec_alloc_context3(nullptr);
    if (!outVideoCodecCtx) {
        std::cerr << "Failed to allocate codec context\n";
        return;
    }

    // 将 codecpar 参数复制到 codec context
    if (avcodec_parameters_to_context(outVideoCodecCtx, codecpar) < 0) {
        std::cerr << "Failed to copy codec parameters to codec context\n";
        return;
    }

    // 设置编码器参数
    outVideoCodecCtx->codec_id = outputFmt->video_codec;
    outVideoCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    outVideoCodecCtx->width = videoDecCtx->width;
    outVideoCodecCtx->height = videoDecCtx->height;
    outVideoCodecCtx->pix_fmt = videoDecCtx->pix_fmt;
    outVideoCodecCtx->time_base = videoDecCtx->time_base;
    outVideoCodecCtx->framerate = videoDecCtx->framerate;

    // 查找编码器并打开
    AVCodec* videoEncoderCodec = avcodec_find_encoder(outVideoCodecCtx->codec_id);
    if (!videoEncoderCodec || avcodec_open2(outVideoCodecCtx, videoEncoderCodec, nullptr) < 0) {
        std::cerr << "Failed to open video encoder\n";
        return;
    }

    // 打开输出文件
    if (!(outputFmtCtx->flags & AVFMT_NOFILE)) {
        if (avio_open(&outputFmtCtx->pb, "output.mp4", AVIO_FLAG_WRITE) < 0) {
            std::cerr << "Failed to open output file\n";
            return;
        }
    }

    // 写文件头
    if (avformat_write_header(outputFmtCtx, nullptr) < 0) {
        std::cerr << "Failed to write output file header\n";
        return;
    }
    // 从视频环形缓冲区中取出帧并编码
    while (videoRingBuf.pop(frame)) {
        if (!frame) continue;

        // 通过 VideoFilter 对解码后的帧进行处理（如旋转）
        vfilter.filterFrame(frame, [&](AVFrame* filteredFrame) {
            if (!filteredFrame) {
                std::cerr << "Filtered frame is null, skipping...\n";
                return;
            }

            // 将处理后的帧传给视频编码器进行编码
            if (!videoEncoder.encode(filteredFrame, videoEncoderQueue)) {
                std::cerr << "[VideoEncodeThread] Video encoding failed\n";
            }

            av_frame_unref(filteredFrame); // 释放过滤后的帧
        });

        av_frame_free(&frame); // 释放解码帧
    }

    // 完成视频编码时，写入所有剩余的数据
    AVPacket* pkt = nullptr;
    while ((pkt = videoEncoderQueue.pop())) {
        if (av_write_frame(outputFmtCtx, pkt) < 0) {
            std::cerr << "Failed to write frame to output file\n";
        }
        av_packet_unref(pkt); // 释放已写入的包
    }

    // 写文件尾并关闭输出文件
    av_write_trailer(outputFmtCtx);
    avio_close(outputFmtCtx->pb);
    avformat_free_context(outputFmtCtx);

    videoEncoderQueue.stop(); // 停止队列
    std::cout << "[VideoEncodeThread] finished\n";
});


    // std::thread videoEncodeThread([&]{
    //     AVFrame* frame = nullptr;

    //     // 从视频环形缓冲区中取出帧并编码
    //     while (videoRingBuf.pop(frame)) {
    //         if (!frame) continue;

    //         // 通过 VideoFilter 对解码后的帧进行处理（如旋转）
    //         vfilter.filterFrame(frame, [&](AVFrame* filteredFrame) {
    //             if (!filteredFrame) {
    //                 std::cerr << "Filtered frame is null, skipping...\n";
    //                 return;
    //             }

    //             // 将处理后的帧传给视频编码器进行编码
    //             if (!videoEncoder.encode(filteredFrame, videoEncoderQueue)) {
    //                 std::cerr << "[VideoEncodeThread] Video encoding failed\n";
    //             }

    //             av_frame_unref(filteredFrame); // 释放过滤后的帧
    //         });

    //         av_frame_free(&frame); // 释放解码帧
    //     }

    //     // 完成视频编码时，清理队列
    //     videoEncoder.flush(videoEncoderQueue);
    //     videoEncoderQueue.stop(); // 停止队列
    //     std::cout << "[VideoEncodeThread] finished\n";
    // });

    // 6. 启动 Demuxer，填充队列
    demuxer.start(audioQueue, videoQueue);
    
    // 7. 等待线程结束
    videoThread.join();
    videoEncodeThread.join();
    std::cout << "Transcode finished\n";
    return 0;
}
