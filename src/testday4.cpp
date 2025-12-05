#include <iostream>
#include <thread>
#include <fstream>

#include "demuxer.h"
#include "queue.h"
#include "videodecoder.h"
#include "audiodecoder.h"
#include "ringbuffer.h"
#include "audiofilter.h"
#include "audioencoder.h"
#include <cstring>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

void saveAC3FromQueue(PacketQueue<AVPacket*>& audioEncoderQueue, const std::string& filename) {
    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs) {
        std::cerr << "Failed to open output file: " << filename << "\n";
        return;
    }

    AVPacket* pkt = nullptr;
    while (true) { // 阻塞直到队列空或停止
        pkt = audioEncoderQueue.pop();
        if (!pkt) break;

        // 直接把编码后的 packet 数据写入文件
        ofs.write(reinterpret_cast<const char*>(pkt->data), pkt->size);

        av_packet_free(&pkt); // 释放 AVPacket
    }

    ofs.close();
    std::cout << "AC3 file saved: " << filename << "\n";
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

    PacketQueue<AVPacket*> audioEncoderQueue;

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
            if (!frame->data[0]) {
                std::cerr << "[AudioThread] invalid frame, skipping\n";
                return;
            }

             // 分配一个新的 AVFrame 并引用原 frame 数据
            AVFrame* copy = av_frame_alloc();
            if (!copy) {
                std::cerr << "[AudioThread] failed to alloc AVFrame\n";
                return;
            }

            // 使用 av_frame_ref 而不是 av_frame_clone
            if (av_frame_ref(copy, frame) < 0) {
                std::cerr << "[AudioThread] failed to ref frame\n";
                av_frame_free(&copy);
                return;
            }

            // 推入环形缓冲区
            audioRingBuf.push(copy);

        });

        // 解码结束，通知缓冲区停止
        audioRingBuf.stop();
        std::cout << "Audio decoding finished\n";
    });

    
    AudioFilter afilter;
    AVCodecContext* audioDecCtx = audioDecoder.getCodecContext();
    double speed = 2.0;

    if (!afilter.init(audioDecCtx, speed, AV_SAMPLE_FMT_FLTP, 0)) {
        std::cerr << "Failed to init AudioFilter\n";
        return -1;
    }

    AudioEncoder audioEncoder;
    int bitrate = 192000; // 可以根据需要调整
    if (!audioEncoder.open(
            audioDecCtx->sample_rate,
            audioDecCtx->channels,
            AV_SAMPLE_FMT_FLTP, // AC3 支持的 float planar
            bitrate
        )) 
    {
        std::cerr << "Failed to open AC3 audio encoder\n";
        return -1;
    }

    std::thread audioEncodeThread([&]{
        AVCodecContext* encCtx = audioEncoder.getCodecContext();
        if (!encCtx) {
            std::cerr << "[AudioEncodeThread] encoder context is null\n";
            return;
        }

        // 缓冲区，用于保证送入编码器的帧长度正确
        int frameSize = encCtx->frame_size; // AC3 通常是 1536
        int channels = encCtx->channels;
        AVSampleFormat fmt = encCtx->sample_fmt;
        uint64_t chLayout = encCtx->channel_layout;
        int sampleRate = encCtx->sample_rate;

        std::vector<float> buffer; // 使用 float，因为我们用 FLTP
        buffer.reserve(frameSize * channels * 2); // 留够两帧的空间

        AVFrame* frame = nullptr;
        while (audioRingBuf.pop(frame)) {
            if (!frame) continue;

            // 先过滤变速
            afilter.filterFrame(frame, [&](AVFrame* f){
                if (!f) return;

                // 检查格式
                if (f->format != fmt || f->channels != channels) {
                    std::cerr << "[AudioEncodeThread] frame format/channel mismatch\n";
                    return;
                }

                // 将 planar float 转成 interleaved float buffer
                for (int i = 0; i < f->nb_samples; i++) {
                    for (int ch = 0; ch < channels; ch++) {
                        float* data = (float*)f->data[ch];
                        buffer.push_back(data[i]);
                    }
                }

                // 当缓冲够一帧时送给编码器
                while ((int)(buffer.size() / channels) >= frameSize) {
                    AVFrame* encFrame = av_frame_alloc();
                    encFrame->nb_samples = frameSize;
                    encFrame->format = fmt;
                    encFrame->channels = channels;
                    encFrame->channel_layout = chLayout;
                    encFrame->sample_rate = sampleRate;

                    if (av_frame_get_buffer(encFrame, 0) < 0) {
                        std::cerr << "[AudioEncodeThread] failed to alloc encFrame buffer\n";
                        av_frame_free(&encFrame);
                        break;
                    }

                    // 填充 encFrame
                    for (int i = 0; i < frameSize; i++) {
                        for (int ch = 0; ch < channels; ch++) {
                            ((float*)encFrame->data[ch])[i] = buffer[i*channels + ch];
                        }
                    }

                    // 移除已送样本
                    buffer.erase(buffer.begin(), buffer.begin() + frameSize * channels);

                    if (!audioEncoder.encode(encFrame, audioEncoderQueue)) {
                        std::cerr << "[AudioEncodeThread] AC3 encode failed\n";
                    }
                    av_frame_free(&encFrame);
                }
            });

            av_frame_free(&frame);
        }

        // flush filter
        afilter.filterFrame(nullptr, [&](AVFrame* f){
            if (!f) return;

            // 剩余样本也加入 buffer
            for (int i = 0; i < f->nb_samples; i++) {
                for (int ch = 0; ch < channels; ch++) {
                    float* data = (float*)f->data[ch];
                    buffer.push_back(data[i]);
                }
            }
            av_frame_free(&f);
        });

        // 将 buffer 剩余样本填充静音凑一帧送编码器
        while (!buffer.empty()) {
            int sendSamples = std::min((int)(buffer.size()/channels), frameSize);
            AVFrame* encFrame = av_frame_alloc();
            encFrame->nb_samples = frameSize;
            encFrame->format = fmt;
            encFrame->channels = channels;
            encFrame->channel_layout = chLayout;
            encFrame->sample_rate = sampleRate;

            if (av_frame_get_buffer(encFrame, 0) < 0) {
                std::cerr << "[AudioEncodeThread] failed to alloc flush frame\n";
                av_frame_free(&encFrame);
                break;
            }

            // 填充数据
            for (int i = 0; i < frameSize; i++) {
                for (int ch = 0; ch < channels; ch++) {
                    float sample = 0.0f;
                    if (i < sendSamples) {
                        sample = buffer[i*channels + ch];
                    }
                    ((float*)encFrame->data[ch])[i] = sample;
                }
            }

            // 移除已使用样本
            if ((int)(buffer.size()/channels) > sendSamples) {
                buffer.erase(buffer.begin(), buffer.begin() + sendSamples*channels);
            } else {
                buffer.clear();
            }

            if (!audioEncoder.encode(encFrame, audioEncoderQueue)) {
                std::cerr << "[AudioEncodeThread] AC3 flush encode failed\n";
            }
            av_frame_free(&encFrame);
        }

        // flush encoder
        audioEncoder.flush(audioEncoderQueue);
        audioEncoderQueue.stop();
        std::cout << "[AudioEncodeThread] finished\n";
    });

    // 6. 启动 Demuxer，填充队列
    demuxer.start(audioQueue, videoQueue);

    // 7. 等待线程结束
    audioThread.join();
    audioEncodeThread.join();
    saveAC3FromQueue(audioEncoderQueue, "audioencoder.ac3");
    std::cout << "Transcode finished\n";
    return 0;
}
