#include "tgaimage.h"
#include "model.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <direct.h>
#include <iomanip>
#include <iostream>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

// ======================= 文件系统小工具 =======================
// GCC 8.1 的 <filesystem> 在 MinGW 下无法编译（libstdc++ fs_path.h 的 Windows
// 分支有 bug），这里只用 CRT 提供本文件需要的少数几个操作。
bool pathExists(const std::string &path) {
    struct _stat st;
    return _stat(path.c_str(), &st) == 0;
}

bool isAbsolutePath(const std::string &path) {
    return (path.size() >= 2 && path[1] == ':')
        || (!path.empty() && (path[0] == '/' || path[0] == '\\'));
}

std::string joinPath(const std::string &dir, const std::string &file) {
    if (dir.empty()) {
        return file;
    }
    const char last = dir.back();
    return (last == '/' || last == '\\') ? dir + file : dir + "/" + file;
}

bool ensureDirectory(const std::string &dir) {
    // 逐级创建，等价于 std::filesystem::create_directories
    for (std::size_t i = 1; i <= dir.size(); ++i) {
        if (i == dir.size() || dir[i] == '/' || dir[i] == '\\') {
            const std::string current = dir.substr(0, i);
            const char last = current.back();
            if (last != ':' && last != '/' && last != '\\') {
                _mkdir(current.c_str()); // 已存在时返回 -1，忽略
            }
        }
    }
    struct _stat st;
    return _stat(dir.c_str(), &st) == 0 && (st.st_mode & _S_IFDIR) != 0;
}

// ======================= 内存测量：全局分配器记账 =======================
// 重载全局 operator new / delete，统计每一次 C++ 动态分配：
//   live        —— 当前还未释放的字节数（用于泄漏检测）
//   peak        —— 自上次 resetPeak() 以来的峰值字节数（用于峰值统计）
//   allocations —— 累计分配次数
// 分配大小写在返回指针前面的 Header 里，delete 时才能精确扣减字节数。
// 说明：这里只统计本项目经过 C++ new/delete 的分配（std::vector、std::string 等）；
//       库内部直接 malloc 的分配以及超对齐分配（align_val_t）不计入。
namespace memtrack {

union Header {
    std::max_align_t align; // 保证返回指针对齐不低于 max_align_t
    std::size_t size;       // 记录本次分配的字节数
};

// 头部按 16 字节取整，保证 new 返回的指针满足默认 new 对齐要求
constexpr std::size_t kAlignment = 16;
constexpr std::size_t kHeaderSize =
    (sizeof(Header) + kAlignment - 1) / kAlignment * kAlignment;

std::atomic<std::size_t> gLive{0};
std::atomic<std::size_t> gPeak{0};
std::atomic<std::size_t> gAllocations{0};
std::atomic<std::size_t> gFrees{0};
std::atomic<std::size_t> gTotalBytes{0};

void onAllocate(std::size_t size) noexcept {
    const std::size_t live = gLive.fetch_add(size, std::memory_order_relaxed) + size;
    gAllocations.fetch_add(1, std::memory_order_relaxed);
    gTotalBytes.fetch_add(size, std::memory_order_relaxed);

    // 峰值只保留最大值；多线程下用 CAS 重试
    std::size_t peak = gPeak.load(std::memory_order_relaxed);
    while (live > peak &&
           !gPeak.compare_exchange_weak(peak, live, std::memory_order_relaxed)) {
    }
}

void onFree(std::size_t size) noexcept {
    gLive.fetch_sub(size, std::memory_order_relaxed);
    gFrees.fetch_add(1, std::memory_order_relaxed);
}

// 把峰值基准重置为当前占用，便于按测试条件分段统计
void resetPeak() noexcept {
    gPeak.store(gLive.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

std::size_t live() noexcept { return gLive.load(std::memory_order_relaxed); }
std::size_t peak() noexcept { return gPeak.load(std::memory_order_relaxed); }
std::size_t allocations() noexcept { return gAllocations.load(std::memory_order_relaxed); }
std::size_t frees() noexcept { return gFrees.load(std::memory_order_relaxed); }
std::size_t totalBytes() noexcept { return gTotalBytes.load(std::memory_order_relaxed); }

} // namespace memtrack

void *operator new(std::size_t size) {
    void *raw = std::malloc(size + memtrack::kHeaderSize);
    if (!raw) throw std::bad_alloc();

    auto *header = static_cast<memtrack::Header *>(raw);
    header->size = size;
    memtrack::onAllocate(size);
    return reinterpret_cast<char *>(raw) + memtrack::kHeaderSize;
}

void *operator new[](std::size_t size) { return ::operator new(size); }

void operator delete(void *ptr) noexcept {
    if (!ptr) return;

    void *raw = reinterpret_cast<char *>(ptr) - memtrack::kHeaderSize;
    auto *header = static_cast<memtrack::Header *>(raw);
    memtrack::onFree(header->size);
    std::free(raw);
}

void operator delete[](void *ptr) noexcept { ::operator delete(ptr); }

void *operator new(std::size_t size, const std::nothrow_t &) noexcept {
    void *raw = std::malloc(size + memtrack::kHeaderSize);
    if (!raw) return nullptr;

    auto *header = static_cast<memtrack::Header *>(raw);
    header->size = size;
    memtrack::onAllocate(size);
    return reinterpret_cast<char *>(raw) + memtrack::kHeaderSize;
}

void *operator new[](std::size_t size, const std::nothrow_t &) noexcept {
    return ::operator new(size, std::nothrow_t{});
}

void operator delete(void *ptr, const std::nothrow_t &) noexcept { ::operator delete(ptr); }
void operator delete[](void *ptr, const std::nothrow_t &) noexcept { ::operator delete[](ptr); }

// 计时用的颜色固定为红色：避免 std::rand() 等与光栅化算法无关的开销混进计时。
constexpr TGAColor benchColor = {0, 0, 255, 255};

// ======================= 待测实现（与 src/main.cpp 逐行一致） =======================
// src/main.cpp 自带 main()，测试目标无法直接链接它，因此这里复制了同一份算法代码，
// 保证计时对象与主程序里的实现完全一致。
// 如果以后修改了 src/main.cpp 里的实现，请同步更新这里，否则对比结果将失去意义。

// 计算边函数
int line_function(int Ax, int Ay, int Bx, int By, int x, int y) {
    return (Bx - Ax) * (y - Ay) - (By - Ay) * (x - Ax);
}

// 计算三角形有向面积
double signed_triangle_area(int ax, int ay, int bx, int by, int cx, int cy) {
    return .5*((by-ay)*(bx+ax) + (cy-by)*(cx+bx) + (ay-cy)*(ax+cx));
}

// 三个点的叉积计算
int cross_product(int ax, int ay, int bx, int by, int cx, int cy) {
    return (bx-ax)*(cy-ay) - (by-ay)*(cx-ax);
}

// Bresenham 画线，triangle_1 依赖它描三角形的三条边。
void line(int ax, int ay, int bx, int by, TGAImage &framebuffer, TGAColor color) {
    // 如果线比较陡（y 跨度大于 x 跨度），以 y 为主步进变量，先转置
    bool steep = std::abs(ax-bx) < std::abs(ay-by);
    if (steep) {
        std::swap(ax, ay);
        std::swap(bx, by);
    }

    // 保证从左往右画
    if (ax > bx) {
        std::swap(ax, bx);
        std::swap(ay, by);
    }

    // 全整数误差法，避免浮点运算
    int y = ay;
    int ierror = 0;
    for (int x=ax; x<=bx; x++) {
        if (steep)
            framebuffer.set(y, x, color);
        else
            framebuffer.set(x, y, color);
        ierror += 2 * std::abs(by-ay);
        y += (by > ay ? 1 : -1) * (ierror > bx - ax);
        ierror -= 2 * (bx-ax)   * (ierror > bx - ax);
    }
}

// 绘制三角形：扫描线填充（自己想的直线方程反函数填充）
void triangle_1(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    // 利用叉积进行背面剔除
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L<=0) return; // 背面

    int min_y = std::min({ay, by, cy});
    int max_y = std::max({ay, by, cy});

    // 逐行扫描，利用反函数填补缺失的像素点
    for (int y = min_y; y <= max_y; ++y) {
        std::vector<float> intersect_x;

        // 遍历三条边，分别求扫描线与边的交点
        auto add_intersection = [&](int x1, int y1, int x2, int y2) {
            // 水平边不参与交点计算
            if (y1 == y2) {
                return;
            }

            // 统一采用左闭右开式的 y 范围，保证顶点不会被相邻两条边重复计算
            int low_y = std::min(y1, y2);
            int high_y = std::max(y1, y2);

            if (y >= low_y && y < high_y) {
                // 线段参数函数，t 表示「从起点走到终点的比例」
                float t = static_cast<float>(y - y1) / (y2 - y1);
                float x = x1 + t * (x2 - x1);
                intersect_x.push_back(x);
            }
        };

        add_intersection(ax, ay, bx, by);
        add_intersection(bx, by, cx, cy);
        add_intersection(ax, ay, cx, cy);

        // 正常情况下，非顶点扫描行会得到两个交点
        if (intersect_x.size() != 2) {
            continue;
        }

        float left = std::min(intersect_x[0], intersect_x[1]);
        float right = std::max(intersect_x[0], intersect_x[1]);

        int x_start = static_cast<int>(std::ceil(left));
        int x_end = static_cast<int>(std::floor(right));

        // 直接填充像素，不再调用 Bresenham
        for (int x = x_start; x <= x_end; ++x) {
            framebuffer.set(x, y, color);
        }
    }
}

// 绘制三角形：扫描线填充，采用线性插值
void triangle_2(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    // 利用叉积进行背面剔除
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L<=0) return; // 背面

    // 按 y 坐标升序排列顶点 a、b、c
    if (ay>by) { std::swap(ax, bx); std::swap(ay, by); }
    if (ay>cy) { std::swap(ax, cx); std::swap(ay, cy); }
    if (by>cy) { std::swap(bx, cx); std::swap(by, cy); }
    int total_height = cy-ay;

    if (ay != by) { // 如果下半部分没有退化
        int segment_height = by - ay;
        for (int y=ay; y<=by; y++) {
            int x1 = ax + ((cx - ax)*(y - ay)) / total_height;
            int x2 = ax + ((bx - ax)*(y - ay)) / segment_height;
            for (int x=std::min(x1,x2); x<std::max(x1,x2); x++)
                framebuffer.set(x, y, color);
        }
    }
    if (by != cy) { // 如果上半部分没有退化
        int segment_height = cy - by;
        for (int y=by; y<=cy; y++) {
            int x1 = ax + ((cx - ax)*(y - ay)) / total_height;
            int x2 = bx + ((cx - bx)*(y - by)) / segment_height;
            for (int x=std::min(x1,x2); x<std::max(x1,x2); x++)
                framebuffer.set(x, y, color);
        }
    }
}

// 绘制三角形：扫描线填充，边函数 + 单方向增量计算
void triangle_3_1(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    int bbminx = std::min(std::min(ax, bx), cx); // 三角形的包围盒
    int bbminy = std::min(std::min(ay, by), cy); // 由左上角和右下角定义
    int bbmaxx = std::max(std::max(ax, bx), cx);
    int bbmaxy = std::max(std::max(ay, by), cy);
    double total_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
    // 利用叉积进行背面剔除
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L<=0) return; // 背面

#pragma omp parallel for
    for(int y=bbminy; y<=bbmaxy; y++){
        int Ep1 = line_function(ax, ay, bx, by, bbminx, y);
        int Ep2 = line_function(bx, by, cx, cy, bbminx, y);
        int Ep3 = line_function(cx, cy, ax, ay, bbminx, y);

        int a1 = ay - by;
        int a2 = by - cy;
        int a3 = cy - ay;
        for(int x=bbminx; x<=bbmaxx; x++){
            if((Ep1>=0 && Ep2>=0 && Ep3>=0) || (Ep1<=0 && Ep2<=0 && Ep3<=0)){
                framebuffer.set(x, y, color);
            }
            Ep1 += a1;
            Ep2 += a2;
            Ep3 += a3;
        }
    }
}

// 绘制三角形：扫描线填充，边函数 + 双方向增量计算
void triangle_3(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    int bbminx = std::min(std::min(ax, bx), cx); // 三角形的包围盒
    int bbminy = std::min(std::min(ay, by), cy); // 由左上角和右下角定义
    int bbmaxx = std::max(std::max(ax, bx), cx);
    int bbmaxy = std::max(std::max(ay, by), cy);
    double total_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
    // 利用叉积进行背面剔除
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L<=0) return; // 背面

    // 初始化边函数值，方便后续做增量计算
    int E1_row = line_function(ax, ay, bx, by, bbminx, bbminy);
    int E2_row = line_function(bx, by, cx, cy, bbminx, bbminy);
    int E3_row = line_function(cx, cy, ax, ay, bbminx, bbminy);

    int a1 = ay - by;
    int a2 = by - cy;
    int a3 = cy - ay;

    int b1 = bx - ax;
    int b2 = cx - bx;
    int b3 = ax - cx;

#pragma omp parallel for
    for(int y=bbminy; y<=bbmaxy; y++){
        // 每一行独立初始化，每一行的初始边函数值应该独立计算
        int Ep1 = E1_row + (y - bbminy) * b1;
        int Ep2 = E2_row + (y - bbminy) * b2;
        int Ep3 = E3_row + (y - bbminy) * b3;
        for(int x=bbminx; x<=bbmaxx; x++){
            if((Ep1>=0 && Ep2>=0 && Ep3>=0) || (Ep1<=0 && Ep2<=0 && Ep3<=0)){
                framebuffer.set(x, y, color);
            }
            Ep1 += a1;
            Ep2 += a2;
            Ep3 += a3;
        }
    }
}

// 绘制三角形：利用重心坐标判断像素是否在三角形内
void triangle_4(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    int bbminx = std::min(std::min(ax, bx), cx); // 三角形的包围盒
    int bbminy = std::min(std::min(ay, by), cy); // 由左上角和右下角定义
    int bbmaxx = std::max(std::max(ax, bx), cx);
    int bbmaxy = std::max(std::max(ay, by), cy);
    double total_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
    // 利用叉积进行背面剔除
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L<=0) return; // 背面

#pragma omp parallel for
    for (int x=bbminx; x<=bbmaxx; x++) {
        for (int y=bbminy; y<=bbmaxy; y++) {
            double alpha = signed_triangle_area(x, y, bx, by, cx, cy) / total_area;
            double beta  = signed_triangle_area(x, y, cx, cy, ax, ay) / total_area;
            double gamma = signed_triangle_area(x, y, ax, ay, bx, by) / total_area;
            if (alpha<0 || beta<0 || gamma<0) continue; // 重心坐标为负 => 该像素在三角形之外
            framebuffer.set(x, y, color);
        }
    }
}

// 绘制三角形：重心坐标 + 增量计算
void triangle_5(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    // 利用叉积进行背面剔除
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L<=0) return; // 背面

    int bbminx = std::min(std::min(ax, bx), cx); // 三角形的包围盒
    int bbminy = std::min(std::min(ay, by), cy); // 由左上角和右下角定义
    int bbmaxx = std::max(std::max(ax, bx), cx);
    int bbmaxy = std::max(std::max(ay, by), cy);

    double triangle_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
    double alpha_area_start = signed_triangle_area(bbminx, bbminy, bx, by, cx, cy);
    double beta_area_start  = signed_triangle_area(bbminx, bbminy, cx, cy, ax, ay);
    double gamma_area_start = signed_triangle_area(bbminx, bbminy, ax, ay, bx, by);

    // X 方向的增量
    double a1 = (by - cy)*.5;
    double a2 = (cy - ay)*.5;
    double a3 = (ay - by)*.5;

    // Y 方向的增量
    double b1 = (cx - bx)*.5;
    double b2 = (ax - cx)*.5;
    double b3 = (bx - ax)*.5;

#pragma omp parallel for
    for (int x=bbminx; x<=bbmaxx; x++) {
        double alpha_init = alpha_area_start + (x - bbminx)*a1;
        double beta_init  = beta_area_start  + (x - bbminx)*a2;
        double gamma_init = gamma_area_start + (x - bbminx)*a3;
        for (int y=bbminy; y<=bbmaxy; y++) {
            double alpha = alpha_init / triangle_area;
            double beta  = beta_init / triangle_area;
            double gamma = gamma_init / triangle_area;
            alpha_init += b1;
            beta_init += b2;
            gamma_init += b3;
            if (alpha<0 || beta<0 || gamma<0) continue; // 重心坐标为负 => 该像素在三角形之外
            framebuffer.set(x, y, color);
        }
    }
}

// ================================ 测试脚手架 ================================

// 统一后的三角形类型：只保留屏幕坐标。
struct BenchTriangle {
    int ax, ay;
    int bx, by;
    int cx, cy;
};

// 待测函数指针类型，与所有 triangle_* 的签名一致。
using RasterizeFn = void (*)(int, int, int, int, int, int, TGAImage &, TGAColor);

// 一个待测对象：名字 + 函数指针。
struct BenchEntry {
    const char *name;
    RasterizeFn fn;
};

// 测试顺序固定，便于结果对照；计时时每轮会交换正序 / 倒序执行。
constexpr std::array<BenchEntry, 6> benchmarks = {{
    {"triangle_1",   triangle_1},
    {"triangle_2",   triangle_2},
    {"triangle_3_1", triangle_3_1},
    {"triangle_3",   triangle_3},
    {"triangle_4",   triangle_4},
    {"triangle_5",   triangle_5},
}};

struct TimingStats {
    double average = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
};

struct PixelCheck {
    std::size_t nonBlack = 0;               // 该函数实际写入的像素数
    std::size_t differentFromReference = 0; // 与基准图颜色不同的像素数
};

// 一个测试条件（一个分辨率 / 一种数量 / 一种大小 / 一种形状）。
struct BenchmarkCase {
    std::string label;
    std::vector<BenchTriangle> triangles;
    int width = 0;
    int height = 0;
    std::string imageName; // 代表图文件名，空则不输出图片
    std::size_t dataBytes = 0; // 三角形数组本身占用的内存（构建时测得）
};

// 单个测试条件的内存指标（单位：字节）。
struct CaseMemoryStats {
    const char *label = nullptr;   // 指向 group.cases 里的 label，不额外分配字符串
    std::size_t dataBytes = 0;     // 该条件三角形数组本身占用
    std::size_t peakBytes = 0;     // 峰值占用 = 数据 + 运行期峰值增量
    std::size_t averageBytes = 0;  // 平均占用 = 数据 + 运行期采样均值
    long long leakedBytes = 0;     // 运行期未回收字节，> 0 视为泄漏
    std::size_t allocations = 0;   // 运行期分配次数
};

// 一组测试（模型、分辨率、数量、大小、形状）。
struct BenchmarkGroup {
    std::string title;
    std::string outputDir;
    int rounds = 1;
    bool verbose = false;      // 是否逐轮打印（模型组使用）
    bool checked = false;      // 是否做像素校验并保存每个实现的图（模型组使用）
    std::vector<BenchmarkCase> cases;
};

// 把模型的所有面一次性转换成屏幕三角形。坐标映射与 src/main.cpp 完全一致：
//   屏幕x = (x + 1) * (width  / 2)
//   屏幕y = (1 - y) * (height / 2)
std::vector<BenchTriangle> buildTriangles(const Model &model, int width, int height) {
    std::vector<BenchTriangle> triangles;
    triangles.reserve(static_cast<std::size_t>(model.nfaces()));

    for (int i = 0; i < model.nfaces(); ++i) {
        const std::array<int, 3> &face = model.face(i);
        std::array<int, 3> sx{};
        std::array<int, 3> sy{};

        for (int j = 0; j < 3; ++j) {
            const Vec3 &v = model.vert(face[j]);
            sx[j] = static_cast<int>((v.x + 1) * (width / 2.f));
            sy[j] = static_cast<int>((1 - v.y) * (height / 2.f));
        }

        triangles.push_back({sx[0], sy[0], sx[1], sy[1], sx[2], sy[2]});
    }
    return triangles;
}

// 把全部三角形绘制一遍，不计时。
void renderAll(const std::vector<BenchTriangle> &triangles, TGAImage &framebuffer, RasterizeFn rasterize) {
    for (const BenchTriangle &t : triangles) {
        rasterize(t.ax, t.ay, t.bx, t.by, t.cx, t.cy, framebuffer, benchColor);
    }
}

// 把全部三角形绘制一遍并返回耗时（毫秒）。
// 帧缓冲由调用方复用：set() 直接覆盖像素，旧内容不影响写入耗时，避免每轮重复构造画布。
double renderAllTimed(const std::vector<BenchTriangle> &triangles, TGAImage &framebuffer, RasterizeFn rasterize) {
    const auto start = std::chrono::steady_clock::now();
    renderAll(triangles, framebuffer, rasterize);
    const auto finish = std::chrono::steady_clock::now();

    return std::chrono::duration<double, std::milli>(finish - start).count();
}

TimingStats summarize(const std::vector<double> &samples) {
    TimingStats stats;
    stats.minimum = *std::min_element(samples.begin(), samples.end());
    stats.maximum = *std::max_element(samples.begin(), samples.end());

    double sum = 0.0;
    for (double sample : samples) {
        sum += sample;
    }
    stats.average = sum / static_cast<double>(samples.size());
    return stats;
}

// 统计非黑像素，并与基准图逐像素比较。
PixelCheck checkAgainstReference(const TGAImage &image, const TGAImage &reference) {
    PixelCheck check;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const TGAColor pixel = image.get(x, y);
            if (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0) {
                ++check.nonBlack;
            }

            const TGAColor ref = reference.get(x, y);
            if (pixel[0] != ref[0] || pixel[1] != ref[1] || pixel[2] != ref[2]) {
                ++check.differentFromReference;
            }
        }
    }
    return check;
}

// 保证三角形是正面朝向（叉积 > 0），否则所有实现的背面剔除都会直接 return。
void ensureFrontFacing(BenchTriangle &triangle) {
    if (cross_product(triangle.ax, triangle.ay, triangle.bx, triangle.by, triangle.cx, triangle.cy) <= 0) {
        std::swap(triangle.bx, triangle.cx);
        std::swap(triangle.by, triangle.cy);
    }
}

constexpr double kPi = 3.14159265358979323846;

// 在画布上生成 count 个随机三角形。
// radiusMin / radiusMax 以 1024 画布为基准，实际会按画布尺寸等比缩放。
std::vector<BenchTriangle> makeRandomTriangles(std::size_t count,
                                               int radiusMin, int radiusMax,
                                               int width, int height,
                                               unsigned seed) {
    const double scale = static_cast<double>(width) / 1024.0;
    const int scaledMin = std::max(1, static_cast<int>(radiusMin * scale));
    const int scaledMax = std::max(scaledMin, static_cast<int>(radiusMax * scale));

    const int margin = std::min(scaledMax + 2, std::max(1, std::min(width, height) / 2 - 1));

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> xDist(margin, width - 1 - margin);
    std::uniform_int_distribution<int> yDist(margin, height - 1 - margin);
    std::uniform_int_distribution<int> radiusDist(scaledMin, scaledMax);
    std::uniform_real_distribution<double> angleDist(0.0, 2.0 * kPi);

    std::vector<BenchTriangle> triangles;
    triangles.reserve(count);

    std::size_t attempts = 0;
    const std::size_t maxAttempts = count * 50;
    while (triangles.size() < count && attempts++ < maxAttempts) {
        const int centerX = xDist(rng);
        const int centerY = yDist(rng);

        std::array<int, 3> xs{};
        std::array<int, 3> ys{};
        for (int j = 0; j < 3; ++j) {
            const double angle = angleDist(rng);
            const int radius = radiusDist(rng);
            xs[j] = std::clamp(centerX + static_cast<int>(std::cos(angle) * radius), 0, width - 1);
            ys[j] = std::clamp(centerY + static_cast<int>(std::sin(angle) * radius), 0, height - 1);
        }

        BenchTriangle triangle{xs[0], ys[0], xs[1], ys[1], xs[2], ys[2]};
        ensureFrontFacing(triangle);

        // 三个点共线时叉积仍为 0，换一个随机三角形重试。
        if (cross_product(triangle.ax, triangle.ay, triangle.bx, triangle.by, triangle.cx, triangle.cy) > 0) {
            triangles.push_back(triangle);
        }
    }
    return triangles;
}

enum class ShapeKind {
    Equilateral,
    Slender,
    Degenerate
};

// 生成指定形状的三角形：
//   等边  -> 三条边接近等长
//   细长  -> 很窄但很高，包围盒扫描量大、实际覆盖面积小
//   退化  -> 三点共线，叉积为 0，会被所有实现的背面剔除直接丢弃
std::vector<BenchTriangle> makeShapeTriangles(ShapeKind kind, std::size_t count,
                                              int width, int height, unsigned seed) {
    const double scale = static_cast<double>(width) / 1024.0;

    int span = 0;
    switch (kind) {
        case ShapeKind::Equilateral:
            span = static_cast<int>(160 * scale);
            break;
        case ShapeKind::Slender:
        case ShapeKind::Degenerate:
            span = static_cast<int>(400 * scale);
            break;
    }
    span = std::clamp(span, 4, std::min(width, height) - 2);

    const int margin = std::min(span + 2, std::max(1, std::min(width, height) / 2 - 1));

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> xDist(margin, width - 1 - margin);
    std::uniform_int_distribution<int> yDist(margin, height - 1 - margin);

    const int equilateralHeight = static_cast<int>(span * std::sqrt(3.0) / 2.0);
    const int slenderWidth = std::max(2, static_cast<int>(12 * scale));

    std::vector<BenchTriangle> triangles;
    triangles.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const int centerX = xDist(rng);
        const int centerY = yDist(rng);

        BenchTriangle triangle{0, 0, 0, 0, 0, 0};
        switch (kind) {
            case ShapeKind::Equilateral:
                triangle = {centerX, centerY - equilateralHeight / 2,
                            centerX - span / 2, centerY + equilateralHeight / 2,
                            centerX + span / 2, centerY + equilateralHeight / 2};
                break;
            case ShapeKind::Slender:
                triangle = {centerX, centerY - span / 2,
                            centerX - slenderWidth / 2, centerY + span / 2,
                            centerX + slenderWidth / 2, centerY + span / 2};
                break;
            case ShapeKind::Degenerate: {
                const int slope = static_cast<int>(span * 0.1);
                triangle = {centerX - span / 2, centerY + slope,
                            centerX, centerY,
                            centerX + span / 2, centerY - slope};
                break;
            }
        }

        if (kind != ShapeKind::Degenerate) {
            ensureFrontFacing(triangle);
        }
        triangles.push_back(triangle);
    }
    return triangles;
}

// 把字节数格式化成便于阅读的字符串（只用于输出，不参与测量）。
std::string formatBytes(long long bytes) {
    char buffer[64];
    const double magnitude = std::fabs(static_cast<double>(bytes));
    if (magnitude >= 1024.0 * 1024.0) {
        std::snprintf(buffer, sizeof(buffer), "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (magnitude >= 1024.0) {
        std::snprintf(buffer, sizeof(buffer), "%.2f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%lld B", bytes);
    }
    return buffer;
}

// 先记录分配前基线，再用工厂构建三角形数组，把净增量写入 dataBytes。
template <typename Factory>
std::vector<BenchTriangle> makeTrianglesTracked(Factory factory, std::size_t &dataBytes) {
    const std::size_t before = memtrack::live();
    std::vector<BenchTriangle> triangles = factory();
    dataBytes = memtrack::live() - before;
    return triangles;
}

// 在组末尾打印内存总览：峰值、平均、增长趋势、泄漏、分配次数。
void printGroupMemorySummary(const BenchmarkGroup &group, const std::vector<CaseMemoryStats> &stats,
                             long long leakedBytes) {
    std::cout << "\n-------------------- 内存总览（" << group.title << "） --------------------\n";

    if (stats.empty()) {
        std::cout << "无条件数据。\n";
        return;
    }

    const CaseMemoryStats *peakCase = &stats.front();
    std::size_t averageSum = 0;
    std::size_t allocationSum = 0;
    for (const CaseMemoryStats &stat : stats) {
        if (stat.peakBytes > peakCase->peakBytes) {
            peakCase = &stat;
        }
        averageSum += stat.averageBytes;
        allocationSum += stat.allocations;
    }
    const std::size_t averageBytes = averageSum / stats.size();

    std::cout << "峰值占用 : " << formatBytes(static_cast<long long>(peakCase->peakBytes))
              << "（条件：" << peakCase->label << "）\n";
    std::cout << "平均占用 : " << formatBytes(static_cast<long long>(averageBytes)) << "\n";

    std::cout << "内存增长 : ";
    if (stats.size() < 2) {
        std::cout << "单个条件无法比较（峰值 "
                  << formatBytes(static_cast<long long>(stats.front().peakBytes)) << "）\n";
    } else {
        for (std::size_t i = 0; i < stats.size(); ++i) {
            if (i != 0) std::cout << " -> ";
            std::cout << stats[i].label << "("
                      << formatBytes(static_cast<long long>(stats[i].peakBytes)) << ")";
        }
        const long long first = static_cast<long long>(stats.front().peakBytes);
        const long long last = static_cast<long long>(stats.back().peakBytes);
        const long long delta = last - first;
        std::cout << "；首末 " << (delta >= 0 ? "+" : "") << formatBytes(delta);
        if (first > 0) {
            std::cout << "（约 " << std::fixed << std::setprecision(2)
                      << static_cast<double>(last) / static_cast<double>(first) << " 倍）";
        }
        std::cout << "\n";
    }

    if (leakedBytes > 0) {
        std::cout << "内存泄漏 : " << formatBytes(leakedBytes) << "（组内临时对象未完全释放）\n";
    } else {
        std::cout << "内存泄漏 : 无（0 B）\n";
    }

    std::cout << "分配次数 : " << allocationSum << " 次\n";
}

// 找到 triangle_3 在 benchmark 表里的下标，作为像素校验的基准实现。
std::size_t findReferenceIndex() {
    for (std::size_t i = 0; i < benchmarks.size(); ++i) {
        if (benchmarks[i].fn == triangle_3) {
            return i;
        }
    }
    return 0;
}

// 运行一整组测试：对组内每个条件，让所有实现各跑 rounds 轮并输出平均值，
// 同时采集该组的内存占用（峰值 / 平均 / 增长 / 泄漏 / 分配次数）。
void runBenchmarkGroup(const BenchmarkGroup &group) {
    std::cout << "\n==================== " << group.title << " ====================\n";
    std::cout << "计时轮数 : " << group.rounds << "\n";

    if (!ensureDirectory(group.outputDir)) {
        std::cerr << "无法创建输出目录 " << group.outputDir << "\n";
    }

    std::cout << std::left << std::setw(16) << "条件" << std::right;
    for (const BenchEntry &entry : benchmarks) {
        std::cout << std::setw(16) << (std::string(entry.name) + "(ms)");
    }
    std::cout << "\n";

    bool anyImageFailed = false;

    // 组级内存基线：用于检测“这组测试本身”是否泄漏。
    // 这里已包含 group.cases 里的三角形数据（它们是调用方构建的、整组常驻，不算泄漏），
    // 同时让 memoryStats 的固定容量分配发生在基线之前，避免被误判为泄漏。
    std::vector<CaseMemoryStats> memoryStats;
    memoryStats.reserve(group.cases.size());
    const std::size_t groupLiveStart = memtrack::live();

    for (const BenchmarkCase &benchmarkCase : group.cases) {
        // 该条件的内存基线：此时三角形数组已在 group 里常驻。
        const std::size_t caseLiveStart = memtrack::live();
        const std::size_t caseAllocStart = memtrack::allocations();
        memtrack::resetPeak();

        // 运行期采样：渲染过程中记录当前占用，用于求平均。
        long long sampleSum = 0;
        std::size_t sampleCount = 0;
        auto sampleMemory = [&]() {
            sampleSum += static_cast<long long>(memtrack::live())
                       - static_cast<long long>(caseLiveStart);
            ++sampleCount;
        };

        {
            TGAImage timingFramebuffer(benchmarkCase.width, benchmarkCase.height, TGAImage::RGB);
            sampleMemory(); // 帧缓冲已分配，作为该条件的第一个采样点

            // 预热一轮（不计时），让 CPU 频率和缓存进入稳定状态。
            for (const BenchEntry &entry : benchmarks) {
                renderAll(benchmarkCase.triangles, timingFramebuffer, entry.fn);
            }
            sampleMemory();

            std::vector<std::vector<double>> samples(
                benchmarks.size(), std::vector<double>(static_cast<std::size_t>(group.rounds)));

            for (int round = 0; round < group.rounds; ++round) {
                // 奇偶轮交换执行顺序，避免固定顺序带来的缓存 / 频率偏差。
                for (std::size_t step = 0; step < benchmarks.size(); ++step) {
                    const std::size_t index = (round % 2 == 0) ? step : (benchmarks.size() - 1 - step);
                    samples[index][round] =
                        renderAllTimed(benchmarkCase.triangles, timingFramebuffer, benchmarks[index].fn);
                    sampleMemory(); // 每种实现的每一轮都采一次
                }

                if (group.verbose) {
                    std::cout << std::setw(4) << (round + 1);
                    for (std::size_t index = 0; index < benchmarks.size(); ++index) {
                        std::cout << std::setw(16) << std::fixed << std::setprecision(2) << samples[index][round];
                    }
                    std::cout << "\n";
                }
            }

            std::vector<TimingStats> stats;
            stats.reserve(benchmarks.size());
            for (const std::vector<double> &functionSamples : samples) {
                stats.push_back(summarize(functionSamples));
            }

            std::size_t fastestIndex = 0;
            for (std::size_t i = 1; i < stats.size(); ++i) {
                if (stats[i].average < stats[fastestIndex].average) {
                    fastestIndex = i;
                }
            }

            std::cout << std::left << std::setw(16) << benchmarkCase.label << std::right;
            for (const TimingStats &stat : stats) {
                std::cout << std::setw(16) << std::fixed << std::setprecision(2) << stat.average;
            }
            std::cout << "\n";

            if (group.verbose) {
                std::cout << "\n函数           平均(ms)     最快(ms)     最慢(ms)     相对最快(倍数)\n";
                for (std::size_t i = 0; i < stats.size(); ++i) {
                    std::cout << std::left << std::setw(14) << benchmarks[i].name << std::right
                              << std::setw(13) << stats[i].average
                              << std::setw(13) << stats[i].minimum
                              << std::setw(13) << stats[i].maximum
                              << std::setw(16) << (stats[i].average / stats[fastestIndex].average) << "\n";
                }
            }

            std::cout << "  最快: " << benchmarks[fastestIndex].name
                      << "（平均 " << stats[fastestIndex].average << " ms）\n";

            if (group.checked) {
                // 模型组：以 triangle_3 为基准做像素校验，并保存每个实现的输出图。
                const std::size_t referenceIndex = findReferenceIndex();
                TGAImage reference(benchmarkCase.width, benchmarkCase.height, TGAImage::RGB);
                renderAll(benchmarkCase.triangles, reference, benchmarks[referenceIndex].fn);
                sampleMemory(); // 基准图已分配

                std::cout << "\n像素校验（基准 = triangle_3）\n";
                std::cout << "注：所有实现都带背面剔除；差异来自填充边界取整不同"
                             "（triangle_1 用闭区间、triangle_2 用半开区间）以及"
                             "triangle_1 会跳过交点数量不为 2 的扫描行，属于预期行为。\n";
                std::cout << "函数           非黑像素     与基准不同像素\n";

                for (std::size_t i = 0; i < benchmarks.size(); ++i) {
                    TGAImage generated;
                    const TGAImage *image = &reference;
                    if (i != referenceIndex) {
                        generated = TGAImage(benchmarkCase.width, benchmarkCase.height, TGAImage::RGB);
                        renderAll(benchmarkCase.triangles, generated, benchmarks[i].fn);
                        image = &generated;
                    }

                    const PixelCheck check = checkAgainstReference(*image, reference);
                    std::cout << std::left << std::setw(14) << benchmarks[i].name << std::right
                              << std::setw(13) << check.nonBlack
                              << std::setw(18) << check.differentFromReference << "\n";

                    const std::string imagePath =
                        joinPath(group.outputDir, "bench_" + std::string(benchmarks[i].name) + ".tga");
                    anyImageFailed = !image->write_tga_file(imagePath, false) || anyImageFailed;
                }
                sampleMemory(); // 基准图 + 当前实现图的峰值时刻
            } else if (!benchmarkCase.imageName.empty()) {
                // 其它组只输出 triangle_3 的代表图，用于肉眼确认该条件下的几何覆盖情况。
                TGAImage image(benchmarkCase.width, benchmarkCase.height, TGAImage::RGB);
                renderAll(benchmarkCase.triangles, image, triangle_3);
                sampleMemory(); // 代表图已分配

                const std::string imagePath = joinPath(group.outputDir, benchmarkCase.imageName);
                anyImageFailed = !image.write_tga_file(imagePath, false) || anyImageFailed;
            }
        }
        // 作用域结束：帧缓冲、校验图片等临时对象都已析构。
        // 此时与 caseLiveStart 的差值就是这段测试新增的未释放内存（正常应为 0）。
        const long long caseLiveEnd = static_cast<long long>(memtrack::live());
        const long long caseLiveStartSigned = static_cast<long long>(caseLiveStart);
        const long long leakedBytes = caseLiveEnd - caseLiveStartSigned;

        const std::size_t peakAbsolute = memtrack::peak();
        const std::size_t peakDelta =
            peakAbsolute > caseLiveStart ? peakAbsolute - caseLiveStart : 0;
        const std::size_t averageDelta = sampleCount == 0
            ? 0
            : static_cast<std::size_t>(std::max<long long>(
                  0, sampleSum / static_cast<long long>(sampleCount)));

        CaseMemoryStats memoryStat;
        memoryStat.label = benchmarkCase.label.c_str();
        memoryStat.dataBytes = benchmarkCase.dataBytes;
        memoryStat.peakBytes = benchmarkCase.dataBytes + peakDelta;
        memoryStat.averageBytes = benchmarkCase.dataBytes + averageDelta;
        memoryStat.leakedBytes = leakedBytes;
        memoryStat.allocations = memtrack::allocations() - caseAllocStart;
        memoryStats.push_back(memoryStat);

        std::cout << "  内存: 峰值 " << formatBytes(static_cast<long long>(memoryStat.peakBytes))
                  << " | 平均 " << formatBytes(static_cast<long long>(memoryStat.averageBytes))
                  << "（数据 " << formatBytes(static_cast<long long>(memoryStat.dataBytes)) << "）"
                  << " | 泄漏 " << (leakedBytes > 0 ? formatBytes(leakedBytes) : std::string("无"))
                  << " | 分配 " << memoryStat.allocations << " 次\n";
    }

    if (!anyImageFailed) {
        std::cout << "结果图片 : " << group.outputDir << "/\n";
    } else {
        std::cerr << "部分结果图片写出失败（当前工作目录可能不可写）\n";
    }

    // 组级泄漏：所有条件的临时对象已释放，与进入条件循环前的占用对比。
    const long long groupLeaked = static_cast<long long>(memtrack::live())
                                - static_cast<long long>(groupLiveStart);
    printGroupMemorySummary(group, memoryStats, groupLeaked);
}

// 模型路径兜底：既支持在仓库根目录运行，也支持在 build/bin 下直接运行。
std::string findModelPath(const std::string &requested) {
    if (pathExists(requested)) {
        return requested;
    }

    if (isAbsolutePath(requested)) {
        return {};
    }

    for (const char *prefix : {"../", "../../", "../../../"}) {
        const std::string candidate = std::string(prefix) + requested;
        if (pathExists(candidate)) {
            return candidate;
        }
    }
    return {};
}

void printUsage(const char *programName) {
    std::cout << "用法: " << programName << " [模型测试轮数=50] [模型路径=obj/diablo3_pose.obj] [基准画布边长=1024]\n";
    std::cout << "派生测试（分辨率/数量/大小/形状）的轮数自动取 max(3, 模型测试轮数 / 20)。\n";
}

int main(int argc, char **argv) {
    int rounds = 50;   // 模型测试的默认轮数
    int size = 1024;
    std::string requestedModel = "obj/diablo3_pose.obj";

    try {
        if (argc > 1) {
            rounds = std::stoi(argv[1]);
        }
        if (argc > 2) {
            requestedModel = argv[2];
        }
        if (argc > 3) {
            size = std::stoi(argv[3]);
        }
    } catch (const std::exception &e) {
        std::cerr << "参数解析失败: " << e.what() << "\n";
        printUsage(argv[0]);
        return 1;
    }

    if (rounds < 1 || size < 16) {
        std::cerr << "参数不合法：轮数至少为 1，画布边长至少为 16。\n";
        printUsage(argv[0]);
        return 1;
    }

    const std::string modelPath = findModelPath(requestedModel);
    if (modelPath.empty()) {
        std::cerr << "找不到模型文件: " << requestedModel << "\n";
        return 1;
    }

    Model model;
    if (!model.load(modelPath)) {
        std::cerr << "模型加载失败: " << modelPath << "\n";
        return 1;
    }

    std::size_t modelBytes = 0;
    std::vector<BenchTriangle> modelTriangles =
        makeTrianglesTracked([&] { return buildTriangles(model, size, size); }, modelBytes);
    const int derivedRounds = std::max(3, rounds / 20);

    std::cout << "====================== 三角形光栅化性能测试 ======================\n";
    std::cout << "模型文件 : " << modelPath << "\n";
    std::cout << "模型规模 : " << model.nverts() << " 个顶点, " << model.nfaces() << " 个三角形\n";
    std::cout << "基准画布 : " << size << " x " << size << "\n";
    std::cout << "模型轮数 : " << rounds << "，派生测试轮数 : " << derivedRounds << "\n";
    std::cout << "测速对象 : triangle_1, triangle_2, triangle_3_1, triangle_3, triangle_4, triangle_5\n";
    std::cout << "内存统计 : 全局 new/delete 记账（峰值 / 平均 / 增长 / 泄漏 / 分配次数）\n";
#ifdef _OPENMP
    std::cout << "OpenMP   : 已启用\n";
#else
    std::cout << "OpenMP   : 未启用（#pragma omp 被忽略，与 src 目标的构建配置一致）\n";
#endif
    std::cout << "输出目录 : out/test/{model,resolution,count,size,shape}\n";

    // 1. 模型测试：完整模型，逐轮输出 + 像素校验 + 每个实现一张图。
    BenchmarkGroup modelGroup;
    modelGroup.title = "模型测试（diablo3_pose.obj）";
    modelGroup.outputDir = "out/test/model";
    modelGroup.rounds = rounds;
    modelGroup.verbose = true;
    modelGroup.checked = true;
    modelGroup.cases.push_back({"diablo3_pose", std::move(modelTriangles), size, size, "", modelBytes});
    runBenchmarkGroup(modelGroup);

    // 2. 分辨率测试：同一模型，画布从 1/4 到 2 倍基准尺寸。
    BenchmarkGroup resolutionGroup;
    resolutionGroup.title = "分辨率测试";
    resolutionGroup.outputDir = "out/test/resolution";
    resolutionGroup.rounds = derivedRounds;

    std::vector<int> resolutions;
    for (int candidate : {size / 4, size / 2, size, size * 2}) {
        if (candidate >= 16 &&
            std::find(resolutions.begin(), resolutions.end(), candidate) == resolutions.end()) {
            resolutions.push_back(candidate);
        }
    }
    for (int resolution : resolutions) {
        const std::string label = std::to_string(resolution) + " x " + std::to_string(resolution);
        const std::string imageName = "res_" + std::to_string(resolution) + ".tga";
        std::size_t dataBytes = 0;
        auto triangles = makeTrianglesTracked(
            [&] { return buildTriangles(model, resolution, resolution); }, dataBytes);
        resolutionGroup.cases.push_back(
            {label, std::move(triangles), resolution, resolution, imageName, dataBytes});
    }
    runBenchmarkGroup(resolutionGroup);

    // 3. 三角形数量测试：固定大小，数量从 100 到 10000。
    BenchmarkGroup countGroup;
    countGroup.title = "三角形数量测试";
    countGroup.outputDir = "out/test/count";
    countGroup.rounds = derivedRounds;

    for (std::size_t count : {static_cast<std::size_t>(100), static_cast<std::size_t>(1000),
                              static_cast<std::size_t>(10000)}) {
        const std::string label = std::to_string(count) + " 个";
        const std::string imageName = "count_" + std::to_string(count) + ".tga";
        std::size_t dataBytes = 0;
        auto triangles = makeTrianglesTracked(
            [&] {
                return makeRandomTriangles(count, 6, 12, size, size,
                                           static_cast<unsigned>(2024 + count));
            },
            dataBytes);
        countGroup.cases.push_back({label, std::move(triangles), size, size, imageName, dataBytes});
    }
    runBenchmarkGroup(countGroup);

    // 4. 三角形大小测试：固定 200 个三角形，只改变单个三角形的大小。
    BenchmarkGroup sizeGroup;
    sizeGroup.title = "三角形大小测试（每种大小 200 个）";
    sizeGroup.outputDir = "out/test/size";
    sizeGroup.rounds = derivedRounds;

    struct SizeSpec {
        const char *label;
        const char *imageName;
        int radiusMin;
        int radiusMax;
    };
    const SizeSpec sizeSpecs[] = {
        {"小", "size_small.tga", 4, 8},
        {"中", "size_medium.tga", 16, 32},
        {"大", "size_large.tga", 48, 96},
    };
    for (const SizeSpec &spec : sizeSpecs) {
        std::size_t dataBytes = 0;
        auto triangles = makeTrianglesTracked(
            [&] {
                return makeRandomTriangles(200, spec.radiusMin, spec.radiusMax, size, size,
                                           static_cast<unsigned>(3000 + spec.radiusMin));
            },
            dataBytes);
        sizeGroup.cases.push_back(
            {spec.label, std::move(triangles), size, size, spec.imageName, dataBytes});
    }
    runBenchmarkGroup(sizeGroup);

    // 5. 三角形形状测试：等边、细长、退化。
    BenchmarkGroup shapeGroup;
    shapeGroup.title = "三角形形状测试";
    shapeGroup.outputDir = "out/test/shape";
    shapeGroup.rounds = derivedRounds;

    struct ShapeSpec {
        ShapeKind kind;
        const char *label;
        const char *imageName;
        std::size_t count;
    };
    const ShapeSpec shapeSpecs[] = {
        {ShapeKind::Equilateral, "等边", "shape_equilateral.tga", 200},
        {ShapeKind::Slender,     "细长", "shape_slender.tga",     500},
        {ShapeKind::Degenerate,  "退化", "shape_degenerate.tga",  20000},
    };
    for (const ShapeSpec &spec : shapeSpecs) {
        std::size_t dataBytes = 0;
        auto triangles = makeTrianglesTracked(
            [&] {
                return makeShapeTriangles(spec.kind, spec.count, size, size,
                                          static_cast<unsigned>(4000 + static_cast<int>(spec.kind)));
            },
            dataBytes);
        shapeGroup.cases.push_back(
            {spec.label, std::move(triangles), size, size, spec.imageName, dataBytes});
    }
    runBenchmarkGroup(shapeGroup);

    std::cout << "\n全部测试完成，结果图片与分类目录都在 out/test/ 下。\n";

    // 结果打印完后暂停，避免程序一闪而过看不到上面的计时数据。
    std::cout << "\n按回车键退出...";
    std::cout.flush();
    std::cin.get();

    return 0;
}
