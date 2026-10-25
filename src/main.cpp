#include "tgaimage.h"
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <utility>

// TGAColor 的通道类型是 std::uint8_t，取值范围 0~255。
// 写成 256 属于常量表达式中的窄化转换（ill-formed），会直接编译失败。
constexpr TGAColor white  = {255, 255, 255, 255};
constexpr TGAColor green  = {  0, 255,   0, 255};
constexpr TGAColor red    = {  0,   0, 255, 255};
constexpr TGAColor blue   = {255, 128,  64, 255};
constexpr TGAColor yellow = {  0, 200, 255, 255};

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
        if (ierror > bx - ax) {//同理
            y += by > ay ? 1 : -1;
            ierror -= 2 * (bx-ax);//同理
        }
    }
}

int main(int argc, char** argv){
    //定义一张图的高和宽
    constexpr int width  = 64;
    constexpr int height = 64;

    //初始化一张tga图片
    TGAImage framebuffer(width, height, TGAImage::RGB);

    //定义三个坐标
    // int ax =  7, ay =  3;
    // int bx = 12, by = 37;
    // int cx = 62, cy = 203;

    // //开始进行绘制
    // line(ax, ay, bx, by, framebuffer, blue);//画线从a到b
    // line(cx, cy, bx, by, framebuffer, green);//画线从c到b
    // line(ax, ay, cx, cy, framebuffer, red);//画线从a到c

    // //在这张tga图片上设置这三个点并带上颜色
    // framebuffer.set(ax, ay, white);
    // framebuffer.set(bx, by, white);
    // framebuffer.set(cx, cy, white);

    std::srand(std::time({}));
    for (int i=0; i<(1<<24); i++) {
        int ax = rand()%width, ay = rand()%height;
        int bx = rand()%width, by = rand()%height;
        TGAColor color = {
            static_cast<std::uint8_t>(rand()%255),
            static_cast<std::uint8_t>(rand()%255),
            static_cast<std::uint8_t>(rand()%255),
            static_cast<std::uint8_t>(rand()%255),
        };
        line(ax, ay, bx, by, framebuffer, color);
    }

    //输出这张图片
    framebuffer.write_tga_file("framebuffer.tga");
    return 0;
}