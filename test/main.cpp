#include "tgaimage.h"
#include "model.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

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
    line(ax, ay, bx, by, framebuffer, color);
    line(bx, by, cx, cy, framebuffer, color);
    line(cx, cy, ax, ay, framebuffer, color);
    double total_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
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
    double total_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
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

// 找到 triangle_3 在 benchmark 表里的下标，作为像素校验的基准实现。
std::size_t findReferenceIndex() {
    for (std::size_t i = 0; i < benchmarks.size(); ++i) {
        if (benchmarks[i].fn == triangle_3) {
            return i;
        }
    }
    return 0;
}

// 运行一整组测试：对组内每个条件，让所有实现各跑 rounds 轮并输出平均值。
void runBenchmarkGroup(const BenchmarkGroup &group) {
    std::cout << "\n==================== " << group.title << " ====================\n";
    std::cout << "计时轮数 : " << group.rounds << "\n";

    std::error_code errorCode;
    std::filesystem::create_directories(group.outputDir, errorCode);
    if (errorCode) {
        std::cerr << "无法创建输出目录 " << group.outputDir << ": " << errorCode.message() << "\n";
    }

    std::cout << std::left << std::setw(16) << "条件" << std::right;
    for (const BenchEntry &entry : benchmarks) {
        std::cout << std::setw(16) << (std::string(entry.name) + "(ms)");
    }
    std::cout << "\n";

    bool anyImageFailed = false;

    for (const BenchmarkCase &benchmarkCase : group.cases) {
        TGAImage timingFramebuffer(benchmarkCase.width, benchmarkCase.height, TGAImage::RGB);

        // 预热一轮（不计时），让 CPU 频率和缓存进入稳定状态。
        for (const BenchEntry &entry : benchmarks) {
            renderAll(benchmarkCase.triangles, timingFramebuffer, entry.fn);
        }

        std::vector<std::vector<double>> samples(
            benchmarks.size(), std::vector<double>(static_cast<std::size_t>(group.rounds)));

        for (int round = 0; round < group.rounds; ++round) {
            // 奇偶轮交换执行顺序，避免固定顺序带来的缓存 / 频率偏差。
            for (std::size_t step = 0; step < benchmarks.size(); ++step) {
                const std::size_t index = (round % 2 == 0) ? step : (benchmarks.size() - 1 - step);
                samples[index][round] =
                    renderAllTimed(benchmarkCase.triangles, timingFramebuffer, benchmarks[index].fn);
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

            std::cout << "\n像素校验（基准 = triangle_3）\n";
            std::cout << "注：所有实现都带背面剔除；triangle_1 先描边再剔除，"
                         "triangle_2 只做填充，因此差异属于预期行为。\n";
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
                    (std::filesystem::path(group.outputDir) / ("bench_" + std::string(benchmarks[i].name) + ".tga")).string();
                anyImageFailed = !image->write_tga_file(imagePath, false) || anyImageFailed;
            }
        } else if (!benchmarkCase.imageName.empty()) {
            // 其它组只输出 triangle_3 的代表图，用于肉眼确认该条件下的几何覆盖情况。
            TGAImage image(benchmarkCase.width, benchmarkCase.height, TGAImage::RGB);
            renderAll(benchmarkCase.triangles, image, triangle_3);

            const std::string imagePath =
                (std::filesystem::path(group.outputDir) / benchmarkCase.imageName).string();
            anyImageFailed = !image.write_tga_file(imagePath, false) || anyImageFailed;
        }
    }

    if (!anyImageFailed) {
        std::cout << "结果图片 : " << group.outputDir << "/\n";
    } else {
        std::cerr << "部分结果图片写出失败（当前工作目录可能不可写）\n";
    }
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

    const std::vector<BenchTriangle> modelTriangles = buildTriangles(model, size, size);
    const int derivedRounds = std::max(3, rounds / 20);

    std::cout << "====================== 三角形光栅化性能测试 ======================\n";
    std::cout << "模型文件 : " << modelPath << "\n";
    std::cout << "模型规模 : " << model.nverts() << " 个顶点, " << model.nfaces() << " 个三角形\n";
    std::cout << "基准画布 : " << size << " x " << size << "\n";
    std::cout << "模型轮数 : " << rounds << "，派生测试轮数 : " << derivedRounds << "\n";
    std::cout << "测速对象 : triangle_1, triangle_2, triangle_3_1, triangle_3, triangle_4, triangle_5\n";
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
    modelGroup.cases.push_back({"diablo3_pose", modelTriangles, size, size, ""});
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
        resolutionGroup.cases.push_back(
            {label, buildTriangles(model, resolution, resolution), resolution, resolution, imageName});
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
        countGroup.cases.push_back({
            label,
            makeRandomTriangles(count, 6, 12, size, size, static_cast<unsigned>(2024 + count)),
            size,
            size,
            imageName
        });
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
        sizeGroup.cases.push_back({
            spec.label,
            makeRandomTriangles(200, spec.radiusMin, spec.radiusMax, size, size,
                                static_cast<unsigned>(3000 + spec.radiusMin)),
            size,
            size,
            spec.imageName
        });
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
        shapeGroup.cases.push_back({
            spec.label,
            makeShapeTriangles(spec.kind, spec.count, size, size,
                               static_cast<unsigned>(4000 + static_cast<int>(spec.kind))),
            size,
            size,
            spec.imageName
        });
    }
    runBenchmarkGroup(shapeGroup);

    std::cout << "\n全部测试完成，结果图片与分类目录都在 out/test/ 下。\n";

    // 结果打印完后暂停，避免程序一闪而过看不到上面的计时数据。
    std::cout << "\n按回车键退出...";
    std::cout.flush();
    std::cin.get();

    return 0;
}
