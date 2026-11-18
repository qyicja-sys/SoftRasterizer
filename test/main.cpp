#include "tgaimage.h"
#include "model/model.h"
#include "camera/camera.h"
#include "pipeline/modelView.h"
#include "pipeline/perspective.h"
#include "pipeline/viewPort.h"
#include "pipeline/clipper.h"
#include "zbuffer/zbuffer.h"
#include "matrix/matrix.h"
#include "vec/Vec.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

// 与 src/main.cpp 中“每个三角形随机取色”的方式一致，不再整帧只填一个颜色。
// 这里用三角形下标生成确定性伪随机色，而不是调用 std::rand()，原因有两点：
//   1. 同一三角形在 A/B/C/D 四个实验、不同轮次中颜色必须完全一致，
//      否则像素校验会把“颜色不同”误算成覆盖差异；
//   2. std::rand() 的全局状态与随机数开销不应混进光栅化计时。
TGAColor triangleColor(std::size_t index) {
    std::uint32_t v = static_cast<std::uint32_t>(index) * 2654435761u;
    v ^= v >> 13;
    v *= 1274126177u;
    v ^= v >> 16;
    return TGAColor{static_cast<std::uint8_t>(v & 0xFF),
                    static_cast<std::uint8_t>((v >> 8) & 0xFF),
                    static_cast<std::uint8_t>((v >> 16) & 0xFF),
                    255};
}

// ======================= 待测实现（四个实验） =======================
//
// 实验设计：
//   A  整数屏幕坐标 + 增量计算         验证旧环境
//   B  小数屏幕坐标 + 增量计算(float)  验证摄像机带来的影响
//   C  小数屏幕坐标 + 每像素直接计算   验证直接计算是否稳定
//   D  小数屏幕坐标 + 增量计算(double) 验证精度改善效果
//
// 屏幕坐标来自视图变换，y 向下，模型正面在屏幕空间的有向面积为负，
// 因此四个实验都只保留“有向面积 < 0”的三角形（与 src/main.cpp 的约定一致）。
// 四个实验统一使用 y 外层 / x 内层的像素遍历顺序，保证差异只来自坐标精度与计算方式。

// 边函数：三角形边 AB 与点 P 构成的平行四边形有向面积（未除以 2）
static inline double edgeFunction(double ax, double ay, double bx, double by,
                                  double px, double py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

// 三角形 PBC 的有向面积（除以 2，与旧实现一致）
static inline double signedArea(double px, double py, double bx, double by,
                                double cx, double cy) {
    return .5 * ((by - py) * (bx + px) + (cy - by) * (cx + bx) + (py - cy) * (px + cx));
}

// 三个点的叉积：用于判断屏幕空间绕序（负 = 正面）
static inline double crossProduct(double ax, double ay, double bx, double by,
                                  double cx, double cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

// 整数版边函数与叉积，供实验 A 使用
static inline int edgeFunctionInt(int ax, int ay, int bx, int by, int px, int py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static inline int crossProductInt(int ax, int ay, int bx, int by, int cx, int cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

// 把小数包围盒裁到屏幕内；三角形与屏幕完全无交集时返回 false。
static bool clippedBounds(double ax, double ay, double bx, double by, double cx, double cy,
                          int width, int height,
                          int &minX, int &minY, int &maxX, int &maxY) {
    const double x0 = std::min({ax, bx, cx});
    const double x1 = std::max({ax, bx, cx});
    const double y0 = std::min({ay, by, cy});
    const double y1 = std::max({ay, by, cy});

    if (x1 < 0.0 || y1 < 0.0 || x0 > width - 1.0 || y0 > height - 1.0) {
        return false;
    }

    minX = std::max(0, static_cast<int>(std::floor(x0)));
    minY = std::max(0, static_cast<int>(std::floor(y0)));
    maxX = std::min(width - 1, static_cast<int>(std::ceil(x1)));
    maxY = std::min(height - 1, static_cast<int>(std::ceil(y1)));
    return minX <= maxX && minY <= maxY;
}

// 实验 A：整数屏幕坐标 + 增量计算（旧环境）。
// 把小数坐标用 static_cast<int> 截断，完全复用旧版 triangle_3 的全整数增量逻辑。
void rasterizeIntegerIncrement(double ax, double ay, double bx, double by,
                               double cx, double cy, TGAImage &framebuffer, TGAColor color) {
    const int iax = static_cast<int>(ax);
    const int iay = static_cast<int>(ay);
    const int ibx = static_cast<int>(bx);
    const int iby = static_cast<int>(by);
    const int icx = static_cast<int>(cx);
    const int icy = static_cast<int>(cy);

    // 利用叉积进行背面剔除；面积 >= 0 视为背面或退化
    const int area2 = crossProductInt(iax, iay, ibx, iby, icx, icy);
    if (area2 >= 0) return;

    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    if (!clippedBounds(iax, iay, ibx, iby, icx, icy,
                       framebuffer.width(), framebuffer.height(),
                       minX, minY, maxX, maxY)) {
        return;
    }

    // 初始化边函数值，后续做增量计算
    const int e1Row = edgeFunctionInt(iax, iay, ibx, iby, minX, minY);
    const int e2Row = edgeFunctionInt(ibx, iby, icx, icy, minX, minY);
    const int e3Row = edgeFunctionInt(icx, icy, iax, iay, minX, minY);

    // X 方向的增量
    const int stepX1 = iay - iby;
    const int stepX2 = iby - icy;
    const int stepX3 = icy - iay;

    // Y 方向的增量
    const int stepY1 = ibx - iax;
    const int stepY2 = icx - ibx;
    const int stepY3 = iax - icx;

#pragma omp parallel for
    for (int y = minY; y <= maxY; ++y) {
        // 每一行独立初始化，避免上一行的状态影响下一行
        int e1 = e1Row + (y - minY) * stepY1;
        int e2 = e2Row + (y - minY) * stepY2;
        int e3 = e3Row + (y - minY) * stepY3;
        for (int x = minX; x <= maxX; ++x) {
            if ((e1 >= 0 && e2 >= 0 && e3 >= 0) || (e1 <= 0 && e2 <= 0 && e3 <= 0)) {
                framebuffer.set(x, y, color);
            }
            e1 += stepX1;
            e2 += stepX2;
            e3 += stepX3;
        }
    }
}

// 实验 B：小数屏幕坐标 + 增量计算（float）。
// 坐标保持小数，但边函数与增量沿用旧代码的 float 类型，用来观察
// 摄像机投影产生的小数坐标进入增量算法后，累积误差有多大。
void rasterizeFloatIncrement(double ax, double ay, double bx, double by,
                             double cx, double cy, TGAImage &framebuffer, TGAColor color) {
    const double area = signedArea(ax, ay, bx, by, cx, cy);
    if (area >= 0.0 || std::abs(area) < 1e-12) return;

    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    if (!clippedBounds(ax, ay, bx, by, cx, cy,
                       framebuffer.width(), framebuffer.height(),
                       minX, minY, maxX, maxY)) {
        return;
    }

    const float fax = static_cast<float>(ax);
    const float fay = static_cast<float>(ay);
    const float fbx = static_cast<float>(bx);
    const float fby = static_cast<float>(by);
    const float fcx = static_cast<float>(cx);
    const float fcy = static_cast<float>(cy);

    const float e1Row = (fbx - fax) * (static_cast<float>(minY) - fay)
                      - (fby - fay) * (static_cast<float>(minX) - fax);
    const float e2Row = (fcx - fbx) * (static_cast<float>(minY) - fby)
                      - (fcy - fby) * (static_cast<float>(minX) - fbx);
    const float e3Row = (fax - fcx) * (static_cast<float>(minY) - fcy)
                      - (fay - fcy) * (static_cast<float>(minX) - fcx);

    const float stepX1 = fay - fby;
    const float stepX2 = fby - fcy;
    const float stepX3 = fcy - fay;

    const float stepY1 = fbx - fax;
    const float stepY2 = fcx - fbx;
    const float stepY3 = fax - fcx;

#pragma omp parallel for
    for (int y = minY; y <= maxY; ++y) {
        float e1 = e1Row + static_cast<float>(y - minY) * stepY1;
        float e2 = e2Row + static_cast<float>(y - minY) * stepY2;
        float e3 = e3Row + static_cast<float>(y - minY) * stepY3;
        for (int x = minX; x <= maxX; ++x) {
            if ((e1 >= 0.f && e2 >= 0.f && e3 >= 0.f) || (e1 <= 0.f && e2 <= 0.f && e3 <= 0.f)) {
                framebuffer.set(x, y, color);
            }
            e1 += stepX1;
            e2 += stepX2;
            e3 += stepX3;
        }
    }
}

// 实验 C：小数屏幕坐标 + 每像素直接计算（double）。
// 每个像素重新计算三个有向面积再除以总面积，不存在跨像素累积误差，作为稳定基准。
void rasterizeDirectPixel(double ax, double ay, double bx, double by,
                          double cx, double cy, TGAImage &framebuffer, TGAColor color) {
    const double area = signedArea(ax, ay, bx, by, cx, cy);
    if (area >= 0.0 || std::abs(area) < 1e-12) return;

    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    if (!clippedBounds(ax, ay, bx, by, cx, cy,
                       framebuffer.width(), framebuffer.height(),
                       minX, minY, maxX, maxY)) {
        return;
    }

#pragma omp parallel for
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const double alpha = signedArea(x, y, bx, by, cx, cy) / area;
            const double beta  = signedArea(x, y, cx, cy, ax, ay) / area;
            const double gamma = signedArea(x, y, ax, ay, bx, by) / area;
            if (alpha < 0.0 || beta < 0.0 || gamma < 0.0) continue; // 重心坐标为负 => 在三角形之外
            framebuffer.set(x, y, color);
        }
    }
}

// 实验 D：小数屏幕坐标 + 增量计算（double）。
// 与实验 B 结构完全相同，只把边函数与增量换成 double，验证精度改善效果。
void rasterizeDoubleIncrement(double ax, double ay, double bx, double by,
                              double cx, double cy, TGAImage &framebuffer, TGAColor color) {
    const double area = signedArea(ax, ay, bx, by, cx, cy);
    if (area >= 0.0 || std::abs(area) < 1e-12) return;

    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    if (!clippedBounds(ax, ay, bx, by, cx, cy,
                       framebuffer.width(), framebuffer.height(),
                       minX, minY, maxX, maxY)) {
        return;
    }

    const double e1Row = edgeFunction(ax, ay, bx, by, minX, minY);
    const double e2Row = edgeFunction(bx, by, cx, cy, minX, minY);
    const double e3Row = edgeFunction(cx, cy, ax, ay, minX, minY);

    const double stepX1 = ay - by;
    const double stepX2 = by - cy;
    const double stepX3 = cy - ay;

    const double stepY1 = bx - ax;
    const double stepY2 = cx - bx;
    const double stepY3 = ax - cx;

#pragma omp parallel for
    for (int y = minY; y <= maxY; ++y) {
        double e1 = e1Row + static_cast<double>(y - minY) * stepY1;
        double e2 = e2Row + static_cast<double>(y - minY) * stepY2;
        double e3 = e3Row + static_cast<double>(y - minY) * stepY3;
        for (int x = minX; x <= maxX; ++x) {
            if ((e1 >= 0.0 && e2 >= 0.0 && e3 >= 0.0) || (e1 <= 0.0 && e2 <= 0.0 && e3 <= 0.0)) {
                framebuffer.set(x, y, color);
            }
            e1 += stepX1;
            e2 += stepX2;
            e3 += stepX3;
        }
    }
}

// ================================ 测试脚手架 ================================

// 统一后的三角形类型：保存小数屏幕坐标。
struct BenchTriangle {
    double ax, ay;
    double bx, by;
    double cx, cy;
};

// 待测函数指针类型，与四个实验的签名一致。
using RasterizeFn = void (*)(double, double, double, double, double, double, TGAImage &, TGAColor);

// 一个待测对象：名字 + 函数指针。
struct BenchEntry {
    const char *name;
    RasterizeFn fn;
};

// 测试顺序固定，便于结果对照；计时时每轮会交换正序 / 倒序执行。
constexpr std::array<BenchEntry, 4> benchmarks = {{
    {"A_int_inc",    rasterizeIntegerIncrement},
    {"B_float_inc",  rasterizeFloatIncrement},
    {"C_direct",     rasterizeDirectPixel},
    {"D_double_inc", rasterizeDoubleIncrement},
}};

struct TimingStats {
    double average = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
};

// 像素统计：非黑像素数，以及分别与实验 A、实验 C 不同的像素数。
struct PixelDiff {
    std::size_t nonBlack = 0;
    std::size_t differentFromA = 0;
    std::size_t differentFromC = 0;
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
    bool checked = false;      // 是否做像素校验并保存每个实验的图（模型组使用）
    std::vector<BenchmarkCase> cases;
};

// 用与 src/main.cpp 相同的管线把模型的面转换成小数屏幕坐标：
//   modelView -> perspective -> 裁剪 -> 透视除法 -> viewPort
// 与旧测试的正交映射不同，这里保留了小数部分，正是“摄像机带来的影响”。
// 相机焦距随画布尺寸等比缩放，保证不同分辨率下模型在画面中的占比一致。
std::vector<BenchTriangle> buildCameraTriangles(Model &model, int width, int height) {
    model.setModelMatrix(Vec3(0, 0, 0), Vec3(0, 0, 0), Vec3(1, 1, 1));

    const float focalLength = 500.0f * static_cast<float>(width) / 1024.0f;
    camera cam(focalLength, width, height);
    // 相机必须在模型前方：worldToCamera 会把 z 取反，position.z 需大于模型最大 z。
    cam.setPosition(Vec3(0, 0, 1));
    cam.setCenter(Vec3(0, 0, 0));
    cam.setUp(Vec3(0, 1, 0));
    cam.setFov();
    cam.setRotation(Vec3(15, 0, 0));

    modelView view;
    view.lookat(cam, model);

    // 先统计相机空间的 z 范围，作为透视矩阵的近远平面。
    ZBuffer zBuffer(width, height);
    zBuffer.setNearPlane(std::numeric_limits<float>::max());
    zBuffer.setFarPlane(0.0f);
    const mat<4, 4, double> mv = view.getModelViewMatrix();
    for (int i = 0; i < model.nverts(); ++i) {
        const float z = (mv * model.vert(i).toVec4()).z;
        if (z > 0.0f) {
            zBuffer.setNearPlane(std::min(zBuffer.getNearPlane(), z));
            zBuffer.setFarPlane(std::max(zBuffer.getFarPlane(), z));
        }
    }
    // 极端情况下（没有有效顶点或近远平面重合）给一组兜底值，避免透视矩阵出现除零。
    if (!(zBuffer.getNearPlane() < zBuffer.getFarPlane())) {
        zBuffer.setNearPlane(0.1f);
        zBuffer.setFarPlane(100.0f);
    }

    perspective persp;
    persp.setCamera(cam);
    persp.perspectiveTransform(zBuffer, width, height);

    viewPort viewport;
    viewport.setWidth(width);
    viewport.setHeight(height);
    viewport.setViewPort();

    const mat<4, 4, double> mvp = persp.getPerspectiveMatrix() * mv;
    const mat<4, 4, double> viewPortMatrix = viewport.getViewPortMatrix();

    std::vector<BenchTriangle> triangles;
    triangles.reserve(static_cast<std::size_t>(model.nfaces()));

    for (int i = 0; i < model.nfaces(); ++i) {
        const std::array<int, 3> &face = model.face(i);

        std::vector<Vec4> clip;
        clip.reserve(3);
        for (int j = 0; j < 3; ++j) {
            const Vec3 &v = model.vert(face[j]);
            clip.push_back(mvp * v.toVec4());
        }

        clipper clipPolygon;
        clipPolygon.isClipped(clip);
        const std::vector<std::vector<Vec4>> readyFace = clipPolygon.triangulation();

        for (const std::vector<Vec4> &triangle : readyFace) {
            bool valid = true;
            std::array<double, 3> screenX{};
            std::array<double, 3> screenY{};
            for (int j = 0; j < 3; ++j) {
                // 裁剪后 w 应为正；退化或仍为非正时直接丢弃这个三角形。
                if (!(triangle[j].w > 0.0f)) {
                    valid = false;
                    break;
                }
                const Vec4 ndc = triangle[j] / triangle[j].w;
                const Vec2 screen = (viewPortMatrix * ndc).getScreenCoord();
                if (!std::isfinite(screen.x) || !std::isfinite(screen.y)) {
                    valid = false;
                    break;
                }
                screenX[j] = screen.x;
                screenY[j] = screen.y;
            }
            if (!valid) continue;

            triangles.push_back({screenX[0], screenY[0],
                                 screenX[1], screenY[1],
                                 screenX[2], screenY[2]});
        }
    }

    return triangles;
}

// 把全部三角形绘制一遍，不计时。
// 每个三角形使用各自的颜色（与 src/main.cpp 的填充方式一致），
// 颜色只由下标决定，因此四个实验画同一批三角形时颜色完全相同。
void renderAll(const std::vector<BenchTriangle> &triangles, TGAImage &framebuffer, RasterizeFn rasterize) {
    for (std::size_t i = 0; i < triangles.size(); ++i) {
        const BenchTriangle &t = triangles[i];
        rasterize(t.ax, t.ay, t.bx, t.by, t.cx, t.cy, framebuffer, triangleColor(i));
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

// 逐像素统计：非黑像素数，以及与实验 A、实验 C 的差异像素数。
PixelDiff compareImages(const TGAImage &image, const TGAImage &imageA, const TGAImage &imageC) {
    PixelDiff diff;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const TGAColor pixel = image.get(x, y);
            if (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0) {
                ++diff.nonBlack;
            }

            const TGAColor a = imageA.get(x, y);
            if (pixel[0] != a[0] || pixel[1] != a[1] || pixel[2] != a[2]) {
                ++diff.differentFromA;
            }

            const TGAColor c = imageC.get(x, y);
            if (pixel[0] != c[0] || pixel[1] != c[1] || pixel[2] != c[2]) {
                ++diff.differentFromC;
            }
        }
    }
    return diff;
}

// 保证三角形是正面朝向（屏幕空间有向面积为负），否则所有实验都会直接丢弃。
void ensureFrontFacing(BenchTriangle &triangle) {
    if (crossProduct(triangle.ax, triangle.ay, triangle.bx, triangle.by,
                     triangle.cx, triangle.cy) >= 0.0) {
        std::swap(triangle.bx, triangle.cx);
        std::swap(triangle.by, triangle.cy);
    }
}

constexpr double kPi = 3.14159265358979323846;

// 在画布上生成 count 个随机三角形（坐标保留小数部分）。
// radiusMin / radiusMax 以 1024 画布为基准，实际会按画布尺寸等比缩放。
std::vector<BenchTriangle> makeRandomTriangles(std::size_t count,
                                               double radiusMin, double radiusMax,
                                               int width, int height,
                                               unsigned seed) {
    const double scale = static_cast<double>(width) / 1024.0;
    const double scaledMin = std::max(1.0, radiusMin * scale);
    const double scaledMax = std::max(scaledMin, radiusMax * scale);

    const double margin = std::min(scaledMax + 2.0,
                                   std::max(1.0, std::min(width, height) / 2.0 - 1.0));

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> xDist(margin, width - 1.0 - margin);
    std::uniform_real_distribution<double> yDist(margin, height - 1.0 - margin);
    std::uniform_real_distribution<double> radiusDist(scaledMin, scaledMax);
    std::uniform_real_distribution<double> angleDist(0.0, 2.0 * kPi);

    std::vector<BenchTriangle> triangles;
    triangles.reserve(count);

    std::size_t attempts = 0;
    const std::size_t maxAttempts = count * 50;
    while (triangles.size() < count && attempts++ < maxAttempts) {
        const double centerX = xDist(rng);
        const double centerY = yDist(rng);

        std::array<double, 3> xs{};
        std::array<double, 3> ys{};
        for (int j = 0; j < 3; ++j) {
            const double angle = angleDist(rng);
            const double radius = radiusDist(rng);
            xs[j] = std::clamp(centerX + std::cos(angle) * radius, 0.0, width - 1.0);
            ys[j] = std::clamp(centerY + std::sin(angle) * radius, 0.0, height - 1.0);
        }

        BenchTriangle triangle{xs[0], ys[0], xs[1], ys[1], xs[2], ys[2]};
        ensureFrontFacing(triangle);

        // 三个点共线时叉积仍为 0，换一个随机三角形重试。
        if (crossProduct(triangle.ax, triangle.ay, triangle.bx, triangle.by,
                         triangle.cx, triangle.cy) < 0.0) {
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

// 生成指定形状的三角形（坐标保留小数）：
//   等边  -> 三条边接近等长
//   细长  -> 很窄但很高，包围盒扫描量大、实际覆盖面积小
//   退化  -> 三点共线，有向面积为 0，会被四个实验直接丢弃
std::vector<BenchTriangle> makeShapeTriangles(ShapeKind kind, std::size_t count,
                                              int width, int height, unsigned seed) {
    const double scale = static_cast<double>(width) / 1024.0;

    double span = 0.0;
    switch (kind) {
        case ShapeKind::Equilateral:
            span = 160.0 * scale;
            break;
        case ShapeKind::Slender:
        case ShapeKind::Degenerate:
            span = 400.0 * scale;
            break;
    }
    span = std::clamp(span, 4.0, static_cast<double>(std::min(width, height)) - 2.0);

    const double margin = std::min(span + 2.0,
                                   std::max(1.0, std::min(width, height) / 2.0 - 1.0));

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> xDist(margin, width - 1.0 - margin);
    std::uniform_real_distribution<double> yDist(margin, height - 1.0 - margin);

    const double equilateralHeight = span * std::sqrt(3.0) / 2.0;
    const double slenderWidth = std::max(2.0, 12.0 * scale);

    std::vector<BenchTriangle> triangles;
    triangles.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const double centerX = xDist(rng);
        const double centerY = yDist(rng);

        BenchTriangle triangle{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        switch (kind) {
            case ShapeKind::Equilateral:
                triangle = {centerX, centerY - equilateralHeight / 2.0,
                            centerX - span / 2.0, centerY + equilateralHeight / 2.0,
                            centerX + span / 2.0, centerY + equilateralHeight / 2.0};
                break;
            case ShapeKind::Slender:
                triangle = {centerX, centerY - span / 2.0,
                            centerX - slenderWidth / 2.0, centerY + span / 2.0,
                            centerX + slenderWidth / 2.0, centerY + span / 2.0};
                break;
            case ShapeKind::Degenerate: {
                const double slope = span * 0.1;
                triangle = {centerX - span / 2.0, centerY + slope,
                            centerX, centerY,
                            centerX + span / 2.0, centerY - slope};
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

// 找到指定实验在 benchmark 表里的下标。
std::size_t findBenchmarkIndex(RasterizeFn fn) {
    for (std::size_t i = 0; i < benchmarks.size(); ++i) {
        if (benchmarks[i].fn == fn) {
            return i;
        }
    }
    return 0;
}

// 运行一整组测试：对组内每个条件，让四个实验各跑 rounds 轮并输出平均值，
// 同时采集该组的内存占用（峰值 / 平均 / 增长 / 泄漏 / 分配次数）。
void runBenchmarkGroup(const BenchmarkGroup &group) {
    std::cout << "\n==================== " << group.title << " ====================\n";
    std::cout << "计时轮数 : " << group.rounds << "\n";

    std::error_code errorCode;
    std::filesystem::create_directories(group.outputDir, errorCode);
    if (errorCode) {
        std::cerr << "无法创建输出目录 " << group.outputDir << ": " << errorCode.message() << "\n";
    }

    std::cout << std::left << std::setw(18) << "条件" << std::right;
    for (const BenchEntry &entry : benchmarks) {
        std::cout << std::setw(18) << (std::string(entry.name) + "(ms)");
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
                    sampleMemory(); // 每个实验的每一轮都采一次
                }

                if (group.verbose) {
                    std::cout << std::setw(4) << (round + 1);
                    for (std::size_t index = 0; index < benchmarks.size(); ++index) {
                        std::cout << std::setw(18) << std::fixed << std::setprecision(2) << samples[index][round];
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

            std::cout << std::left << std::setw(18) << benchmarkCase.label << std::right;
            for (const TimingStats &stat : stats) {
                std::cout << std::setw(18) << std::fixed << std::setprecision(2) << stat.average;
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
                // 模型组：以 A（旧环境）和 C（每像素直接计算）为对照，统计四个实验的像素差异。
                const std::size_t oldIndex = findBenchmarkIndex(rasterizeIntegerIncrement);
                const std::size_t referenceIndex = findBenchmarkIndex(rasterizeDirectPixel);

                TGAImage imageA(benchmarkCase.width, benchmarkCase.height, TGAImage::RGB);
                renderAll(benchmarkCase.triangles, imageA, benchmarks[oldIndex].fn);
                TGAImage imageC(benchmarkCase.width, benchmarkCase.height, TGAImage::RGB);
                renderAll(benchmarkCase.triangles, imageC, benchmarks[referenceIndex].fn);
                sampleMemory(); // A / C 两张对照图已分配

                std::cout << "\n像素校验（对照 A = 整数坐标 + 增量，基准 C = 小数坐标 + 每像素直接计算）\n";
                std::cout << "注：A 先把小数坐标截断成整数，差异来自取整；\n"
                             "    B 保留小数但增量用 float，与 C 的差异反映摄像机小数坐标下的累积误差；\n"
                             "    D 用 double 做增量，与 C 的差异应明显小于 B。\n";
                std::cout << "函数           非黑像素       与A不同       与C不同\n";

                for (std::size_t i = 0; i < benchmarks.size(); ++i) {
                    const TGAImage *image = nullptr;
                    TGAImage generated;
                    if (i == oldIndex) {
                        image = &imageA;
                    } else if (i == referenceIndex) {
                        image = &imageC;
                    } else {
                        generated = TGAImage(benchmarkCase.width, benchmarkCase.height, TGAImage::RGB);
                        renderAll(benchmarkCase.triangles, generated, benchmarks[i].fn);
                        image = &generated;
                    }

                    const PixelDiff diff = compareImages(*image, imageA, imageC);
                    std::cout << std::left << std::setw(14) << benchmarks[i].name << std::right
                              << std::setw(13) << diff.nonBlack
                              << std::setw(15) << diff.differentFromA
                              << std::setw(15) << diff.differentFromC << "\n";

                    const std::string imagePath =
                        (std::filesystem::path(group.outputDir) / ("bench_" + std::string(benchmarks[i].name) + ".tga")).string();
                    anyImageFailed = !image->write_tga_file(imagePath, false) || anyImageFailed;
                }
                sampleMemory(); // A / C / 当前实验图共存时的峰值时刻
            } else if (!benchmarkCase.imageName.empty()) {
                // 其它组只输出 C 的代表图，用于肉眼确认该条件下的几何覆盖情况。
                TGAImage image(benchmarkCase.width, benchmarkCase.height, TGAImage::RGB);
                renderAll(benchmarkCase.triangles, image, rasterizeDirectPixel);
                sampleMemory(); // 代表图已分配

                const std::string imagePath =
                    (std::filesystem::path(group.outputDir) / benchmarkCase.imageName).string();
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
    if (std::filesystem::exists(requested)) {
        return requested;
    }

    const std::filesystem::path requestedPath(requested);
    if (requestedPath.is_absolute()) {
        return {};
    }

    for (const char *prefix : {"../", "../../", "../../../"}) {
        const std::filesystem::path candidate = std::filesystem::path(prefix) / requestedPath;
        if (std::filesystem::exists(candidate)) {
            return candidate.string();
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

    // 模型组的小数屏幕坐标由摄像机 + 透视投影 + 视口变换真实生成。
    std::size_t modelBytes = 0;
    std::vector<BenchTriangle> modelTriangles =
        makeTrianglesTracked([&] { return buildCameraTriangles(model, size, size); }, modelBytes);
    const int derivedRounds = std::max(3, rounds / 20);

    std::cout << "====================== 三角形光栅化精度实验 ======================\n";
    std::cout << "模型文件 : " << modelPath << "\n";
    std::cout << "模型规模 : " << model.nverts() << " 个顶点, " << model.nfaces() << " 个三角形\n";
    std::cout << "基准画布 : " << size << " x " << size << "\n";
    std::cout << "模型轮数 : " << rounds << "，派生测试轮数 : " << derivedRounds << "\n";
    std::cout << "实验对象 : A 整数坐标+int增量（旧环境） | B 小数坐标+float增量（摄像机影响）\n";
    std::cout << "           C 小数坐标+逐像素直接计算（稳定基准） | D 小数坐标+double增量（精度改善）\n";
    std::cout << "内存统计 : 全局 new/delete 记账（峰值 / 平均 / 增长 / 泄漏 / 分配次数）\n";
#ifdef _OPENMP
    std::cout << "OpenMP   : 已启用\n";
#else
    std::cout << "OpenMP   : 未启用（#pragma omp 被忽略，与 src 目标的构建配置一致）\n";
#endif
    std::cout << "输出目录 : out/test/{model,resolution,count,size,shape}\n";

    // 1. 模型测试：完整模型，逐轮输出 + 像素校验 + 每个实验一张图。
    BenchmarkGroup modelGroup;
    modelGroup.title = "模型测试（diablo3_pose.obj，摄像机投影后的小数屏幕坐标）";
    modelGroup.outputDir = "out/test/model";
    modelGroup.rounds = rounds;
    modelGroup.verbose = true;
    modelGroup.checked = true;
    modelGroup.cases.push_back({"diablo3_pose", std::move(modelTriangles), size, size, "", modelBytes});
    runBenchmarkGroup(modelGroup);

    // 2. 分辨率测试：同一模型与视角，画布从 1/4 到 2 倍基准尺寸。
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
            [&] { return buildCameraTriangles(model, resolution, resolution); }, dataBytes);
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
                return makeRandomTriangles(count, 6.0, 12.0, size, size,
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
        double radiusMin;
        double radiusMax;
    };
    const SizeSpec sizeSpecs[] = {
        {"小", "size_small.tga", 4.0, 8.0},
        {"中", "size_medium.tga", 16.0, 32.0},
        {"大", "size_large.tga", 48.0, 96.0},
    };
    for (const SizeSpec &spec : sizeSpecs) {
        std::size_t dataBytes = 0;
        auto triangles = makeTrianglesTracked(
            [&] {
                return makeRandomTriangles(200, spec.radiusMin, spec.radiusMax, size, size,
                                           static_cast<unsigned>(3000 + static_cast<int>(spec.radiusMin)));
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
