#pragma once
#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
}

class VideoFilter {
public:
    VideoFilter();
    ~VideoFilter();

    // angle 取值示例：
    // 0: pass-through
    // 90: rotate clockwise
    // 180: upside down
    // 270: rotate counter-clockwise
    bool init(AVCodecContext* decCtx, int angle);

    // 输入 frame，输出经过滤镜处理后的 frame
    // 回调返回过滤后的帧
    void filterFrame(AVFrame* frame, std::function<void(AVFrame*)> callback);

private:
    bool initFilterGraph(AVCodecContext* decCtx, int angle);

    AVFilterGraph* filterGraph_ = nullptr;
    AVFilterContext* buffersrcCtx_ = nullptr;
    AVFilterContext* buffersinkCtx_ = nullptr;

    int rotateAngle_ = 0;
};
