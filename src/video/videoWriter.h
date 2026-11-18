#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

struct TGAImage;

// 视频输出模块：把一系列帧通过管道交给 ffmpeg 编码成 MP4。
//
// 为什么用管道而不是链接 libav*：
//   本项目的工具链是 i686（32 位）MinGW，而 Windows 下官方 FFmpeg 开发库
//   基本只提供 64 位版本，直接链接会失败。管道方式与编译器架构无关，
//   只需一个可执行的 ffmpeg.exe，模块内部把原始 BGR 帧写到它的 stdin。
//
// 帧率、分辨率、时长都在 Config 里配置；分辨率必须与传入的 TGAImage 一致。
class VideoWriter {
public:
    struct Config {
        int width = 1024;              // 视频宽度（像素），必须为偶数
        int height = 1024;             // 视频高度（像素），必须为偶数
        int fps = 30;                  // 帧率
        double durationSeconds = 5.0;  // 视频时长（秒），总帧数 = round(fps * duration)
        std::string outputPath = "out/render.mp4";
        std::string ffmpegPath = "ffmpeg";  // ffmpeg.exe 路径；也可只写 "ffmpeg" 走 PATH
        int crf = 18;                  // x264 质量，数值越小画质越高、文件越大
    };

    explicit VideoWriter(const Config& config);
    ~VideoWriter();

    // 内部持有管道句柄，禁止拷贝
    VideoWriter(const VideoWriter&) = delete;
    VideoWriter& operator=(const VideoWriter&) = delete;

    // 启动 ffmpeg 子进程并打开输出管道。参数非法或 ffmpeg 无法启动时返回 false。
    bool open();

    // 写入一帧。支持 RGB/RGBA 和灰度图：灰度图会自动复制到 R/G/B 三个通道。
    // 帧尺寸与配置不一致、或尚未 open() 时返回 false。
    bool writeFrame(const TGAImage& frame);

    // 关闭管道并等待 ffmpeg 退出。返回 ffmpeg 退出码是否为 0。
    bool close();

    int totalFrames() const;    // 按 fps 与时长计算的目标帧数
    int writtenFrames() const;  // 已经成功写入的帧数

private:
    Config config;
    // 用 void* 保存 Windows 句柄，避免在头文件里引入 windows.h
    void* pipeWrite = nullptr;  // HANDLE：写往 ffmpeg 的 stdin
    void* process = nullptr;    // HANDLE：ffmpeg 子进程
    int written = 0;
    bool failed = false;
};
