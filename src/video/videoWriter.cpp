#include "video/videoWriter.h"
#include "tgaimage.h"

// CreateProcess / 匿名管道实现，必须先关掉 windows.h 的 min/max 宏
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {
// 给路径加双引号，避免 ffmpeg.exe 或输出目录带空格时被拆成多个参数
std::string quote(const std::string& value) {
    return "\"" + value + "\"";
}
}  // namespace

VideoWriter::VideoWriter(const Config& config) : config(config) {}

VideoWriter::~VideoWriter() {
    if (pipeWrite != nullptr || process != nullptr) {
        close();
    }
}

int VideoWriter::totalFrames() const {
    if (config.fps <= 0 || config.durationSeconds <= 0.0) {
        return 0;
    }
    return std::max(1, static_cast<int>(std::lround(config.fps * config.durationSeconds)));
}

int VideoWriter::writtenFrames() const {
    return written;
}

bool VideoWriter::open() {
    if (pipeWrite != nullptr || process != nullptr) {
        std::cerr << "[VideoWriter] 管道已经打开，不能重复 open()" << std::endl;
        return false;
    }
    if (config.width <= 0 || config.height <= 0 || config.fps <= 0 || config.durationSeconds <= 0.0) {
        std::cerr << "[VideoWriter] 参数非法：宽高、帧率、时长都必须为正数" << std::endl;
        return false;
    }
    // x264 的 yuv420p 要求宽高都是偶数，否则 ffmpeg 会直接报错退出
    if (config.width % 2 != 0 || config.height % 2 != 0) {
        std::cerr << "[VideoWriter] 宽高必须为偶数（yuv420p 编码要求）："
                  << config.width << "x" << config.height << std::endl;
        return false;
    }
    if (totalFrames() <= 0) {
        std::cerr << "[VideoWriter] 总帧数为 0，请检查帧率与时长" << std::endl;
        return false;
    }

    // ffmpeg 不会自动创建父目录，这里先保证输出目录存在
    const std::filesystem::path outputPath(config.outputPath);
    if (outputPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(outputPath.parent_path(), ec);
        if (ec) {
            std::cerr << "[VideoWriter] 无法创建输出目录：" << outputPath.parent_path().string()
                      << "，" << ec.message() << std::endl;
            return false;
        }
    }

    // 只有在路径里带有目录分隔符时才检查文件是否存在；
    // 只写 "ffmpeg" 的情况交给系统按 PATH 查找。
    const std::filesystem::path ffmpegPath(config.ffmpegPath);
    if (ffmpegPath.has_parent_path() && !std::filesystem::exists(ffmpegPath)) {
        std::cerr << "[VideoWriter] 找不到 ffmpeg：" << ffmpegPath.string()
                  << "\n请把 ffmpeg.exe 放到该路径，或通过 --ffmpeg 指定可执行文件。" << std::endl;
        return false;
    }

    // -f rawvideo 表示从 stdin 读取未压缩帧；
    // -pix_fmt bgr24 与 TGAImage 的内存布局（B、G、R）完全一致；
    // -r 指定输入帧率，输出时长由写入的总帧数决定。
    std::string command =
        quote(config.ffmpegPath) +
        " -y -loglevel error" +
        " -f rawvideo -pix_fmt bgr24" +
        " -s " + std::to_string(config.width) + "x" + std::to_string(config.height) +
        " -r " + std::to_string(config.fps) +
        " -i -" +
        " -an -c:v libx264 -preset medium -pix_fmt yuv420p -crf " + std::to_string(config.crf) +
        " -movflags +faststart " +
        quote(config.outputPath);

    // 创建可继承的匿名管道：读端交给 ffmpeg 的 stdin，写端留在父进程
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &attributes, 0)) {
        std::cerr << "[VideoWriter] 创建管道失败，错误码：" << GetLastError() << std::endl;
        return false;
    }
    // 写端不能被子进程继承，否则父进程关闭后 ffmpeg 等不到 EOF
    SetHandleInformation(writePipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = readPipe;
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    // CreateProcess 要求命令行可写，先复制成字符缓冲区
    std::vector<char> commandLine(command.begin(), command.end());
    commandLine.push_back('\0');

    PROCESS_INFORMATION processInfo{};
    const BOOL created = CreateProcessA(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &processInfo);
    CloseHandle(readPipe);
    if (!created) {
        CloseHandle(writePipe);
        std::cerr << "[VideoWriter] 启动 ffmpeg 失败（错误码 " << GetLastError() << "）："
                  << command << std::endl;
        return false;
    }
    CloseHandle(processInfo.hThread);

    pipeWrite = writePipe;
    process = processInfo.hProcess;

    std::cout << "[VideoWriter] 输出 " << config.outputPath
              << "，" << config.width << "x" << config.height
              << " @ " << config.fps << "fps，共 " << totalFrames() << " 帧" << std::endl;
    return true;
}

bool VideoWriter::writeFrame(const TGAImage& frame) {
    if (pipeWrite == nullptr || process == nullptr || failed) {
        return false;
    }
    if (frame.width() != config.width || frame.height() != config.height) {
        std::cerr << "[VideoWriter] 帧尺寸不匹配：期望 "
                  << config.width << "x" << config.height
                  << "，实际 " << frame.width() << "x" << frame.height() << std::endl;
        failed = true;
        return false;
    }

    // 按行组装成 bgr24，一行只调用一次 WriteFile，避免每像素写一次管道
    std::vector<std::uint8_t> row(static_cast<std::size_t>(config.width) * 3);
    for (int y = 0; y < config.height; ++y) {
        for (int x = 0; x < config.width; ++x) {
            const TGAColor color = frame.get(x, y);
            std::uint8_t b = color[0];
            std::uint8_t g = color[1];
            std::uint8_t r = color[2];
            // 灰度图的 get() 返回 bytespp = 1，只有 bgra[0] 有效
            if (color.bytespp == 1) {
                g = b;
                r = b;
            }
            std::size_t offset = static_cast<std::size_t>(x) * 3;
            row[offset]     = b;
            row[offset + 1] = g;
            row[offset + 2] = r;
        }

        DWORD bytesWritten = 0;
        const DWORD rowBytes = static_cast<DWORD>(row.size());
        if (!WriteFile(static_cast<HANDLE>(pipeWrite), row.data(), rowBytes, &bytesWritten, nullptr)
            || bytesWritten != rowBytes) {
            std::cerr << "[VideoWriter] 写入管道失败（ffmpeg 可能已提前退出）" << std::endl;
            failed = true;
            return false;
        }
    }
    ++written;
    return true;
}

bool VideoWriter::close() {
    // 先关闭写端：ffmpeg 读到 EOF 后才会把编码器收尾并写出文件
    if (pipeWrite != nullptr) {
        CloseHandle(static_cast<HANDLE>(pipeWrite));
        pipeWrite = nullptr;
    }
    if (process == nullptr) {
        return !failed;
    }

    WaitForSingleObject(static_cast<HANDLE>(process), INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(static_cast<HANDLE>(process), &exitCode);
    CloseHandle(static_cast<HANDLE>(process));
    process = nullptr;

    if (failed) {
        return false;
    }
    if (exitCode != 0) {
        std::cerr << "[VideoWriter] ffmpeg 退出码：" << exitCode << std::endl;
        return false;
    }
    std::cout << "[VideoWriter] 已写入 " << written << " 帧" << std::endl;
    return true;
}
