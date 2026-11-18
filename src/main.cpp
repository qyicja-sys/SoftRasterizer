#include "tgaimage.h"
#include "model/model.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <utility>
#include "zbuffer/zbuffer.h"
#include "camera/camera.h"
#include "pipeline/modelview.h"
#include "pipeline/perspective.h"
#include "pipeline/clipper.h"
#include "pipeline/viewport.h"
#include "pipeline/rasterize.h"
#include "video/videoWriter.h"
#include <filesystem>
#include <limits>
#include <string>
#include <exception>



// TGAColor 的通道类型是 std::uint8_t，取值范围 0~255。
// 写成 256 属于常量表达式中的窄化转换（ill-formed），会直接编译失败。
constexpr TGAColor white  = {255, 255, 255, 255};
constexpr TGAColor green  = {  0, 255,   0, 255};
constexpr TGAColor red    = {  0,   0, 255, 255};
constexpr TGAColor blue   = {255, 128,  64, 255};
constexpr TGAColor yellow = {  0, 200, 255, 255};

// 画布（帧缓冲）尺寸。现在由命令行参数决定，因此不再用 constexpr：
// 程序启动时会按视频配置重新给它们赋值，并重建 ZBuffer。
int width  = 1024;
int height = 1024;
ZBuffer zBuffer(width, height);


//按重心坐标((1-t),t)权重，来进行绘制，由近到远(A-B)
void line(int ax, int ay, int bx, int by, TGAImage &framebuffer, TGAColor color) {
    // for (float t=0.; t<1.; t+=.01) {
    //     int x = std::round( ax + (bx-ax)*t );
    //     int y = std::round( ay + (by-ay)*t );
    //     framebuffer.set(x, y, color);
    // }
    // 如果起点在终点的右边，就把它们对调一下
    // 如果线比较陡（y 跨度大于 x 跨度），我们以 y 为主步进变量
    bool steep = std::abs(ax-bx) < std::abs(ay-by);
    if (steep) { // 如果线段较陡，就转置图像
        std::swap(ax, ay);
        std::swap(bx, by);
    }

    if (ax > bx) {
        std::swap(ax, bx); // 交换 x 坐标
        std::swap(ay, by); // 同时交换对应的 y 坐标，保持线段不变
    }

    //最简单的运算
    // for (int x=ax; x<=bx; x++) {
    //     float t = (x-ax) / static_cast<float>(bx-ax);//计算当前点占整条线段的比例
    //     int y = std::round( ay + (by-ay)*t );
    //     if (steep) // 如果之前转置过，就转置回来
    //         framebuffer.set(y, x, color);
    //     else
    //         framebuffer.set(x, y, color);
    // }

    //优化1
    // float y = ay;
    // for (int x=ax; x<=bx; x++) {
    //     if (steep) // 如果之前转置过，就转置回来
    //         framebuffer.set(y, x, color);
    //     else
    //         framebuffer.set(x, y, color);
    //     y += (by-ay) / static_cast<float>(bx-ax);//斜率
    // }

    //浮点数乘法和强制类型转换在底层非常消耗 CPU 性能，所以需要进一步优化
    //优化2

    // int y = ay;
    // float error = 0;//用来计算斜率
    // for (int x=ax; x<=bx; x++) {
    //     if (steep) // 如果之前转置过，就转置回来
    //         framebuffer.set(y, x, color);
    //     else
    //         framebuffer.set(x, y, color);
    //     error += std::abs(by-ay)/static_cast<float>(bx-ax);
    //     if (error>.5) {
    //         y += by > ay ? 1 : -1;//决定走向，向下还是向上
    //         error -= 1.;//把已经兑现的误差抵消掉，保留多扣除的部分。如果不减去1，error 会一直累加，越来越大，y 就会疯狂地 +1
    //     }
    // }

    //优化3
    //消除浮点运算，采用Bresenham 画线算法（或者叫全整数误差法）
    int y = ay;
    int ierror = 0;
    for (int x=ax; x<=bx; x++) {
        if (steep) // 如果之前转置过，就转置回来
            framebuffer.set(y, x, color);
        else
            framebuffer.set(x, y, color);
        ierror += 2 * std::abs(by-ay);//原error += abs(by - ay) / (bx - ax)式子两边同时乘以Δx(bx - ax)，消除除法。实际就是对优化2的式子进行等价交换
        // if (ierror > bx - ax) {//同理
        //     y += by > ay ? 1 : -1;
        //     ierror -= 2 * (bx-ax);//同理
        // }
        //进一步优化，if判断会拖慢性能
        y += (by > ay ? 1 : -1) * (ierror > bx - ax);
        ierror -= 2 * (bx-ax)   * (ierror > bx - ax);
    }
}

//计算边函数
int line_function(int Ax, int Ay, int Bx, int By, int x, int y) {
    return (Bx - Ax) * (y - Ay) - (By - Ay) * (x - Ax);
}

//计算三角形有向面积(平行四边形，需要除以二，这里没有除是方便其它使用)
double signed_triangle_area(int x, int y, int bx, int by, int cx, int cy) {
    return .5*((by-y)*(bx+x) + (cy-by)*(cx+bx) + (y-cy)*(x+cx));
}

//三个点的叉积计算
int cross_product(int ax, int ay, int bx, int by, int cx, int cy) {
    return (bx-ax)*(cy-ay) - (by-ay)*(cx-ax);
}

//绘制三角形
//扫描线填充(我自己想的直线方程反函数填充，可以优化)
void triangle_1(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L > 0) return; // 背面（屏幕空间顺时针） 
    // if (total_area<1) return; // 背面剔除 + 丢弃覆盖面积不足一个像素的三角形

    int min_y = std::min({ay, by, cy});
    int max_y = std::max({ay, by, cy});

    // 3. 逐行扫描，原先为一元函数，现在改利用反函数，填补缺失的像素点
    for (int y = min_y; y <= max_y; ++y) {
        std::vector<float> intersect_x;

        // 遍历三条边，分别求扫描线与边的交点
        auto add_intersection = [&](int x1, int y1, int x2, int y2) {
            // 水平边不参与交点计算
            if (y1 == y2) {
                return;
            }

            // 统一采用左闭右开式的 y 范围
            // 保证顶点不会被相邻两条边重复计算
            int low_y = std::min(y1, y2);
            int high_y = std::max(y1, y2);

            if (y >= low_y && y < high_y) {
                //线段参数函数，t 表示「从起点走到终点的比例」
                float t = static_cast<float>(y - y1) / (y2 - y1);
                float x = x1 + t * (x2 - x1);
                intersect_x.push_back(x);
            }
        };

        //判断扫描线落在哪两条边，直接无脑三次就不用判断具体是哪两条了
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
            // 替换成你项目中实际的像素写入接口
            framebuffer.set(x, y, color);
        }
    }
}

//绘制三角形(扫描线填充，采用线性插值)
void triangle_2(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    // 利用叉积进行背面剔除
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L > 0) return; // 背面（屏幕空间顺时针） 
    // if (total_area<1) return; // 背面剔除 + 丢弃覆盖面积不足一个像素的三角形

    // 按 y 坐标升序排列顶点 a、b、c（冒泡排序，好耶！）
    if (ay>by) { std::swap(ax, bx); std::swap(ay, by); }
    if (ay>cy) { std::swap(ax, cx); std::swap(ay, cy); }
    if (by>cy) { std::swap(bx, cx); std::swap(by, cy); }
    int total_height = cy-ay;

    if (ay != by) { // 如果下半部分没有退化
        int segment_height = by - ay;
        for (int y=ay; y<=by; y++) { // 从 ay 到 by 扫描水平线
            int x1 = ax + ((cx - ax)*(y - ay)) / total_height;
            int x2 = ax + ((bx - ax)*(y - ay)) / segment_height;
            for (int x=std::min(x1,x2); x<std::max(x1,x2); x++)  // 画一条水平线
                framebuffer.set(x, y, color);
        }
    }
    if (by != cy) { // 如果上半部分没有退化
        int segment_height = cy - by;
        for (int y=by; y<=cy; y++) { // 从 by 到 cy 扫描水平线
            int x1 = ax + ((cx - ax)*(y - ay)) / total_height;
            int x2 = bx + ((cx - bx)*(y - by)) / segment_height;
            for (int x=std::min(x1,x2); x<std::max(x1,x2); x++)  // 画一条水平线
                framebuffer.set(x, y, color);
        }
    }
}

//绘制三角形(扫描线填充，边函数+单方向增量计算)
void triangle_3_1(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    int bbminx = std::min(std::min(ax, bx), cx); // 三角形的包围盒
    int bbminy = std::min(std::min(ay, by), cy); // 由左上角和右下角定义
    int bbmaxx = std::max(std::max(ax, bx), cx);
    int bbmaxy = std::max(std::max(ay, by), cy);
    double total_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
    // 利用叉积进行背面剔除
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L > 0) return; // 背面（屏幕空间顺时针） 
    // if (total_area<1) return; // 背面剔除 + 丢弃覆盖面积不足一个像素的三角形

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

//绘制三角形(扫描线填充，边函数+双方向增量计算)
void triangle_3(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    int bbminx = std::min(std::min(ax, bx), cx); // 三角形的包围盒
    int bbminy = std::min(std::min(ay, by), cy); // 由左上角和右下角定义
    int bbmaxx = std::max(std::max(ax, bx), cx);
    int bbmaxy = std::max(std::max(ay, by), cy);
    double total_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
    // 利用叉积进行背面剔除
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L > 0) return; // 背面（屏幕空间顺时针）
    // if (total_area<1) return; // 背面剔除 + 丢弃覆盖面积不足一个像素的三角形

    // 初始化边函数值，方便后续做增量计算
    int E1_row = line_function(ax, ay, bx, by, bbminx, bbminy);
    int E2_row = line_function(bx, by, cx, cy, bbminx, bbminy);
    int E3_row = line_function(cx, cy, ax, ay, bbminx, bbminy);

    //X方向的增量
    int a1 = ay - by;
    int a2 = by - cy;
    int a3 = cy - ay;

    //Y方向的增量
    int b1 = bx - ax;
    int b2 = cx - bx;
    int b3 = ax - cx;

#pragma omp parallel for
    for(int y=bbminy; y<=bbmaxy; y++){
        // 每一行独立初始化，每一行的初始边函数值应该独立计算，不能直接让上一行的变量状态影响下一行
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

//绘制三角形面积，利用重心坐标判断像素是否在三角形内
void triangle_4(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    int bbminx = std::min(std::min(ax, bx), cx); // 三角形的包围盒
    int bbminy = std::min(std::min(ay, by), cy); // 由左上角和右下角定义
    int bbmaxx = std::max(std::max(ax, bx), cx);
    int bbmaxy = std::max(std::max(ay, by), cy);
    double total_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
    // 利用叉积进行背面剔除
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L > 0) return; // 背面（屏幕空间顺时针）
    // if (total_area<1) return; // 背面剔除 + 丢弃覆盖面积不足一个像素的三角形

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

//绘制三角形面积，利用重心坐标判断像素是否在三角形内(再修版，增加增量计算提升性能)
void triangle_5(int ax, int ay, int bx, int by, int cx, int cy, float az, float bz, float cz, TGAImage &framebuffer, TGAColor color) {
    // 利用叉积进行背面剔除。
    // 注意：屏幕坐标的 y 轴向下，投影时用了 (1 - v.y) 把模型 y 翻转过来，
    // 这个镜像变换会把三角形绕序整体反号（模型空间逆时针的正面，到屏幕上变成顺时针）。
    // 所以这里要保留叉积为负的面，叉积为正的反而是背面，不能再沿用原来的 L<=0。
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L > 0) return; // 背面（屏幕空间顺时针）

    int bbminx = std::max(0, std::min({ax, bx, cx}));            // 三角形的包围盒
    int bbminy = std::max(0, std::min({ay, by, cy}));            // 由左上角和右下角定义
    int bbmaxx = std::min(width  - 1, std::max({ax, bx, cx}));
    int bbmaxy = std::min(height - 1, std::max({ay, by, cy}));

    double triangle_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
    if(std::abs(triangle_area) < 1e-12) return; // 退化三角形（三点共线/重合），面积可正可负

    double alpha_area_start =signed_triangle_area(bbminx, bbminy, bx, by, cx, cy);
    double beta_area_start =signed_triangle_area(bbminx, bbminy, cx, cy, ax, ay);
    double gamma_area_start =signed_triangle_area(bbminx, bbminy, ax, ay, bx, by);

    //X方向的增量
    double a1 = (by - cy)*.5;
    double a2 = (cy - ay)*.5;
    double a3 = (ay - by)*.5;

    //Y方向的增量
    double b1 = (cx - bx)*.5;
    double b2 = (ax - cx)*.5;
    double b3 = (bx - ax)*.5;
    

#pragma omp parallel for
    for (int x=bbminx; x<=bbmaxx; x++) {
        double alpha_init = alpha_area_start + (x - bbminx)*a1;
        double beta_init = beta_area_start + (x - bbminx)*a2;
        double gamma_init = gamma_area_start + (x - bbminx)*a3;
        for (int y=bbminy; y<=bbmaxy; y++) {
            double alpha = alpha_init / triangle_area;
            double beta  = beta_init / triangle_area;
            double gamma = gamma_init / triangle_area;
            alpha_init += b1;
            beta_init += b2;
            gamma_init += b3;
            if (alpha<0 || beta<0 || gamma<0) continue; // 重心坐标为负 => 该像素在三角形之外
            unsigned char z = static_cast<unsigned char>(alpha * az + beta * bz + gamma * cz);
            if(!zBuffer.testAndSet(x, y, z)) continue;
            framebuffer.set(x, y, color);
        }
    }
}

//顶点颜色属性插值渲染
void triangle_6_interpolation(int ax, int ay, int bx, int by, int cx, int cy, int az, int bz, int cz, TGAColor aColor, TGAColor bColor, TGAColor cColor, TGAImage &framebuffer) {
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L > 0) return; // 背面（屏幕空间顺时针）
    int bbminx = std::max(0, std::min({ax, bx, cx}));            // 三角形的包围盒
    int bbminy = std::max(0, std::min({ay, by, cy}));            // 由左上角和右下角定义
    int bbmaxx = std::min(width  - 1, std::max({ax, bx, cx}));
    int bbmaxy = std::min(height - 1, std::max({ay, by, cy}));
    if(bbminx > bbmaxx || bbminy > bbmaxy) return; // 包围盒与屏幕无交集

    double triangle_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
    if(std::abs(triangle_area) < 1e-12) return; // 退化三角形（三点共线/重合），面积可正可负

    double alpha_area_start =signed_triangle_area(bbminx, bbminy, bx, by, cx, cy);
    double beta_area_start =signed_triangle_area(bbminx, bbminy, cx, cy, ax, ay);
    double gamma_area_start =signed_triangle_area(bbminx, bbminy, ax, ay, bx, by);

    //X方向的增量
    double a1 = (by - cy)*.5;
    double a2 = (cy - ay)*.5;
    double a3 = (ay - by)*.5;

    //Y方向的增量
    double b1 = (cx - bx)*.5;
    double b2 = (ax - cx)*.5;
    double b3 = (bx - ax)*.5;
    

#pragma omp parallel for
    for (int x=bbminx; x<=bbmaxx; x++) {
        double alpha_init = alpha_area_start + (x - bbminx)*a1;
        double beta_init = beta_area_start + (x - bbminx)*a2;
        double gamma_init = gamma_area_start + (x - bbminx)*a3;
        for (int y=bbminy; y<=bbmaxy; y++) {
            double alpha = alpha_init / triangle_area;
            double beta  = beta_init / triangle_area;
            double gamma = gamma_init / triangle_area;
            alpha_init += b1;
            beta_init += b2;
            gamma_init += b3;
            if (alpha<0 || beta<0 || gamma<0) continue; // 重心坐标为负 => 该像素在三角形之外
            //unsigned char z = static_cast<unsigned char>(alpha * az + beta * bz + gamma * cz);
            TGAColor color;

            for (int i = 0; i < 3; i++) {
                color[i] = static_cast<uint8_t>(std::round(
                    (aColor[i] * alpha / az + bColor[i] * beta / bz + cColor[i] * gamma / cz) / 
                    (alpha/az + beta/bz + gamma/cz)
                ));
            }
            color[3] = static_cast<uint8_t>(255);
            framebuffer.set(x, y, color);
        }
    }
}

void triangle_6_grey_scale_Map(int ax, int ay, int bx, int by, int cx, int cy, float az, float bz, float cz, TGAImage &framebuffer) {
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L > 0) return; // 背面（屏幕空间顺时针）

    int bbminx = std::max(0, std::min({ax, bx, cx}));            // 裁剪到屏幕范围
    int bbminy = std::max(0, std::min({ay, by, cy}));
    int bbmaxx = std::min(width  - 1, std::max({ax, bx, cx}));
    int bbmaxy = std::min(height - 1, std::max({ay, by, cy}));
    if(bbminx > bbmaxx || bbminy > bbmaxy) return; // 包围盒与屏幕无交集

    double triangle_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
    if(std::abs(triangle_area) < 1e-12) return; // 退化三角形（三点共线/重合）
    double alpha_area_start =signed_triangle_area(bbminx, bbminy, bx, by, cx, cy);
    double beta_area_start =signed_triangle_area(bbminx, bbminy, cx, cy, ax, ay);
    double gamma_area_start =signed_triangle_area(bbminx, bbminy, ax, ay, bx, by);

    //X方向的增量
    double a1 = (by - cy)*.5;
    double a2 = (cy - ay)*.5;
    double a3 = (ay - by)*.5;

    //Y方向的增量
    double b1 = (cx - bx)*.5;
    double b2 = (ax - cx)*.5;
    double b3 = (bx - ax)*.5;
    

#pragma omp parallel for
    for (int x=bbminx; x<=bbmaxx; x++) {
        double alpha_init = alpha_area_start + (x - bbminx)*a1;
        double beta_init = beta_area_start + (x - bbminx)*a2;
        double gamma_init = gamma_area_start + (x - bbminx)*a3;
        for (int y=bbminy; y<=bbmaxy; y++) {
            double alpha = alpha_init / triangle_area;
            double beta  = beta_init / triangle_area;
            double gamma = gamma_init / triangle_area;
            alpha_init += b1;
            beta_init += b2;
            gamma_init += b3;
            if (alpha<0 || beta<0 || gamma<0) continue; // 重心坐标为负 => 该像素在三角形之外
            double depth = alpha * az + beta * bz + gamma * cz;
            unsigned char z = static_cast<unsigned char>(std::clamp(depth, 0.0, 254.0));
            if(!zBuffer.testAndSet(x, y, z)){
                continue;
            }
            framebuffer.set(x, y, {static_cast<uint8_t>(255 - z)}); // 近亮远暗
        }
    }
}

// 视频画面模式
enum class OutputMode {
    DepthOnly,    // 只输出深度图
    ColorOnly,    // 只输出颜色图
    SideBySide,   // 左颜色、右深度
    TopBottom     // 上颜色、下深度
};

// 由面下标生成稳定的颜色：同一个三角形每帧颜色相同，视频不会闪烁
TGAColor faceColor(int faceIndex) {
    const std::uint32_t hash = static_cast<std::uint32_t>(faceIndex) * 2654435761u;
    return TGAColor{
        static_cast<std::uint8_t>(64 + (hash & 0xBF)),
        static_cast<std::uint8_t>(64 + ((hash >> 8) & 0xBF)),
        static_cast<std::uint8_t>(64 + ((hash >> 16) & 0xBF)),
        255};
}

// 把颜色图和深度图按模式合成一帧；深度图是灰度，写入 RGB 帧时复制到三个通道
TGAImage composeFrame(const TGAImage& colorMap, const TGAImage& depthMap, OutputMode mode) {
    if (mode == OutputMode::ColorOnly) return colorMap;
    if (mode == OutputMode::DepthOnly) return depthMap;

    const bool sideBySide = (mode == OutputMode::SideBySide);
    const int outWidth   = sideBySide ? colorMap.width() * 2 : colorMap.width();
    const int outHeight  = sideBySide ? colorMap.height() : colorMap.height() * 2;
    const int depthOffsetX = sideBySide ? colorMap.width() : 0;
    const int depthOffsetY = sideBySide ? 0 : colorMap.height();

    TGAImage frame(outWidth, outHeight, TGAImage::RGB);
    for (int y = 0; y < colorMap.height(); ++y) {
        for (int x = 0; x < colorMap.width(); ++x) {
            frame.set(x, y, colorMap.get(x, y));
            const TGAColor gray = depthMap.get(x, y);
            frame.set(x + depthOffsetX, y + depthOffsetY,
                      TGAColor{gray[0], gray[0], gray[0], 255});
        }
    }
    return frame;
}

// 渲染一帧：更新模型矩阵、重算相机空间近远平面，然后光栅化所有三角形。
// 颜色图和深度图使用各自独立的 rasterize / ZBuffer，避免共用深度缓冲时
// 第二个 pass 被第一个 pass 写入的深度全部拒绝。
void renderFrame(Model& model, camera& cam,
                 rasterize& colorRasterizer, rasterize& depthRasterizer,
                 modelView& mvNode, perspective& persp, clipper& clipNode,
                 viewPort& viewportNode,
                 ZBuffer& colorZBuffer, ZBuffer& depthZBuffer,
                 const Vec3& position, const Vec3& rotation, const Vec3& scale,
                 bool needColor, bool needDepth,
                 TGAImage& colorMap, TGAImage& depthMap) {
    model.setModelMatrix(position, rotation, scale);
    mvNode.lookat(cam, model);

    // 每帧都要重新统计相机空间 z 范围：模型旋转后最近/最远点会变化，
    // 沿用固定值会让透视深度映射失准。
    float nearPlane = std::numeric_limits<float>::max();
    float farPlane = 0;
    const mat<4,4,double> mv = mvNode.getModelViewMatrix();
    for (int i = 0; i < model.nverts(); ++i) {
        float z = (mv * model.vert(i).toVec4()).z;
        if (z > 0) {
            nearPlane = std::min(nearPlane, z);
            farPlane  = std::max(farPlane,  z);
        }
    }

    // 两个深度缓冲都清空并设置相同的近远平面
    colorZBuffer.clear();
    colorZBuffer.setNearPlane(nearPlane);
    colorZBuffer.setFarPlane(farPlane);
    depthZBuffer.clear();
    depthZBuffer.setNearPlane(nearPlane);
    depthZBuffer.setFarPlane(farPlane);

    colorRasterizer.rasterizeSet(mvNode, persp, clipNode, viewportNode, colorZBuffer, model);
    colorRasterizer.rasterizeInit(cam, width, height);
    depthRasterizer.rasterizeSet(mvNode, persp, clipNode, viewportNode, depthZBuffer, model);
    depthRasterizer.rasterizeInit(cam, width, height);

    // 每帧重新分配并刷黑，避免上一帧的像素残留
    colorMap = TGAImage(width, height, TGAImage::RGB);
    depthMap = TGAImage(width, height, TGAImage::GRAYSCALE);

    // 遍历模型里的每个三角形面（本模型共 5022 个）。
    // face(i) 返回的是 3 个“顶点下标”，不是坐标；
    // 必须再用 vert(下标) 查表，才能拿到真正的 3D 坐标。
    for (int i = 0; i < model.nfaces(); ++i) {
        const std::array<int,3> modelFace = model.face(i);

        std::vector<Vec4> clipVertices;
        clipVertices.reserve(3);
        for (int j = 0; j < 3; ++j) {
            Vec4 nowVec4 = model.vert(modelFace.at(j)).toVec4();
            clipVertices.push_back(depthRasterizer.rasterizeGetClipVec(nowVec4));
        }

        clipper clipPolygon;
        clipPolygon.isClipped(clipVertices);
        vector<vector<Vec4>> readyRasterizeFace = clipPolygon.triangulation();

        const TGAColor color = faceColor(i);
        for (size_t k = 0; k < readyRasterizeFace.size(); ++k) {
            if (needColor) {
                colorRasterizer.rasterize_colorMap(readyRasterizeFace[k], colorMap, color);
            }
            if (needDepth) {
                depthRasterizer.rasterize_depthMap(readyRasterizeFace[k], depthMap);
            }
        }
    }
}

int main(int argc, char** argv){
    // ---------- 视频配置：默认值可被命令行参数覆盖 ----------
    // 用法示例：
    //   SoftRasterizer.exe --width 800 --height 800 --fps 30 --duration 5
    VideoWriter::Config videoConfig;
    videoConfig.outputPath = "out/diablo3_pose_turntable.mp4";
    OutputMode mode = OutputMode::SideBySide;

    // 优先使用项目内下载的 ffmpeg；不存在时回退到系统 PATH 里的 ffmpeg
    if (std::filesystem::exists("tools/ffmpeg/bin/ffmpeg.exe")) {
        videoConfig.ffmpegPath = "tools/ffmpeg/bin/ffmpeg.exe";
    }

    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const bool hasValue = (i + 1 < argc);
            if (arg == "--fps" && hasValue) {
                videoConfig.fps = std::stoi(argv[++i]);
            } else if (arg == "--width" && hasValue) {
                videoConfig.width = std::stoi(argv[++i]);
            } else if (arg == "--height" && hasValue) {
                videoConfig.height = std::stoi(argv[++i]);
            } else if (arg == "--duration" && hasValue) {
                videoConfig.durationSeconds = std::stod(argv[++i]);
            } else if (arg == "--output" && hasValue) {
                videoConfig.outputPath = argv[++i];
            } else if (arg == "--ffmpeg" && hasValue) {
                videoConfig.ffmpegPath = argv[++i];
            } else if (arg == "--mode" && hasValue) {
                const std::string value = argv[++i];
                if (value == "depth") {
                    mode = OutputMode::DepthOnly;
                } else if (value == "color") {
                    mode = OutputMode::ColorOnly;
                } else if (value == "side") {
                    mode = OutputMode::SideBySide;
                } else if (value == "top") {
                    mode = OutputMode::TopBottom;
                } else {
                    std::cerr << "未知模式：" << value
                              << "，可选 depth / color / side / top" << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "未知参数：" << arg << "\n"
                          << "可用参数：--mode depth|color|side|top --fps N --width N --height N "
                          << "--duration 秒 --output 文件 --ffmpeg 路径"
                          << std::endl;
                return 1;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "参数解析失败：" << error.what()
                  << "\n请检查 --width/--height/--fps/--duration 是否为正整数或小数。" << std::endl;
        return 1;
    }

    // 渲染分辨率由 --width/--height 决定；拼接模式会让最终视频尺寸翻倍
    const int renderWidth  = videoConfig.width;
    const int renderHeight = videoConfig.height;
    if (mode == OutputMode::SideBySide) videoConfig.width  *= 2;
    if (mode == OutputMode::TopBottom)  videoConfig.height *= 2;

    width  = renderWidth;
    height = renderHeight;
    zBuffer = ZBuffer(width, height);

    // 颜色图和深度图是两张独立的图像
    TGAImage colorMap(width, height, TGAImage::RGB);
    TGAImage depthMap(width, height, TGAImage::GRAYSCALE);

    // 颜色和深度各用一个独立的深度缓冲，否则两个 pass 会互相拒绝对方的像素
    ZBuffer colorZBuffer(width, height);
    ZBuffer depthZBuffer(width, height);

    rasterize colorRasterizer;
    rasterize depthRasterizer;
    modelView newModelView;
    perspective newPerspective;
    clipper newClipper;
    viewPort newViewport;

    camera newCamera(500, width, height);
    // 分辨率只改变像素密度，不改变构图：
    // fov 由 focalLength 和画面高度共同决定，若固定 focalLength，
    // 降低分辨率会收窄视野、把模型放大出画面。
    // 这里以 1024 高度为基准等比缩放焦距，保证不同分辨率下看到相同的范围。
    newCamera.setFocalLength(500.0f * height / 1024.0f);
    // 相机必须在模型前方：worldToCamera 里 z 取反后，要求 position.z > 模型最大 z。
    // 放在原点等于站在模型内部，一半顶点 z_cam <= 0 被判为无效，
    // 另一半 z_cam 趋近 0，透视除法后坐标放大到屏幕外，最终仍是一片黑。
    newCamera.setPosition(Vec3(0, 0, 1));
    newCamera.setCenter(Vec3(0, 0, 0));
    newCamera.setUp(Vec3(0, 1, 0));
    newCamera.setFov();

    newCamera.setRotation(Vec3(0, 0, 0));//这里保存的是旋转坐标

    // 加载 OBJ 模型。路径是相对“当前工作目录”的：
    // VSCode 的 launch.json 把 cwd 设为项目根目录，所以能找到 obj/；
    // 如果直接在 build/bin 目录下启动 exe，会因找不到文件而加载失败。
    Model newModel;
    bool flag = newModel.load("obj/diablo3_pose.obj");
    if(!flag){
        std::cerr << "模型加载失败" << std::endl;
        return 1;
    }

    // 只看数量不够——如果某个字段读错位，数量仍然可能对，但坐标会整体错掉。
    Vec3 firstVect = newModel.vert(0);
    std::cout << "第一个顶点：" << firstVect.x << " " << firstVect.y <<  " " << firstVect.z << std::endl;

    VideoWriter writer(videoConfig);
    if (!writer.open()) {
        return 1;
    }

    // 模型绕 Y 轴匀速旋转整整一圈，总帧数决定每帧角度，首尾可无缝循环
    const bool needColor = (mode != OutputMode::DepthOnly);
    const bool needDepth = (mode != OutputMode::ColorOnly);
    const int totalFrames = writer.totalFrames();
    Vec3 position = {0,0,0};
    Vec3 rotation = {0,0,0};
    Vec3 scale = {0.8,0.8,0.8};

    for (int frame = 0; frame < totalFrames; ++frame) {
        rotation.y = static_cast<float>(360.0 * frame / totalFrames);
        renderFrame(newModel, newCamera, colorRasterizer, depthRasterizer,
                    newModelView, newPerspective, newClipper, newViewport,
                    colorZBuffer, depthZBuffer, position, rotation, scale,
                    needColor, needDepth, colorMap, depthMap);

        // 按模式合成：单独输出，或把颜色图和深度图左右/上下拼在一起
        TGAImage frameImage = composeFrame(colorMap, depthMap, mode);
        if (!writer.writeFrame(frameImage)) {
            writer.close();
            return 1;
        }
        if ((frame + 1) % 30 == 0 || frame + 1 == totalFrames) {
            std::cout << "已渲染 " << (frame + 1) << "/" << totalFrames << " 帧" << std::endl;
        }
    }

    if (!writer.close()) {
        return 1;
    }

    // 输出最后一帧 TGA，保留单帧检查方式。
    // 第二个参数 vflip 必须显式传 false：
    // write_tga_file 默认 vflip=true，会把文件头声明成“左下角为原点”，
    // 看图软件于是把内存第 0 行画在画面底部，相当于又上下翻了一次。
    if (needColor) colorMap.write_tga_file("out/diablo3_pose_color.tga", false);
    if (needDepth) depthMap.write_tga_file("out/diablo3_pose_depth.tga", false);
    zBuffer.clear();

    return 0;
}
