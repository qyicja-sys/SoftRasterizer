#include "tgaimage.h"
#include <cmath>

// TGAColor 的通道类型是 std::uint8_t，取值范围 0~255。
// 写成 256 属于常量表达式中的窄化转换（ill-formed），会直接编译失败。
constexpr TGAColor white = {255, 255, 255, 255};
constexpr TGAColor green   = {  0, 255,   0, 255};
constexpr TGAColor red     = {  0,   0, 255, 255};
constexpr TGAColor blue    = {255, 128,  64, 255};
constexpr TGAColor yellow  = {  0, 200, 255, 255};

//按重心坐标((1-t),t)权重，来进行绘制，由近到远(A-B)
void line(int ax, int ay, int bx, int by, TGAImage &framebuffer, TGAColor color) {
    for (float t=0.; t<1.; t+=.02) {
        int x = std::round( ax + (bx-ax)*t );
        int y = std::round( ay + (by-ay)*t );
        framebuffer.set(x, y, color);
    }
}

int main(int argc, char** argv){
    //定义一张图的高和宽
    constexpr int width  = 128;
    constexpr int height = 128;

    //初始化一张tga图片
    TGAImage framebuffer(width, height, TGAImage::RGB);

    //定义三个坐标
    int ax =  7, ay =  3;
    int bx = 12, by = 37;
    int cx = 62, cy = 53;

    //开始进行绘制
    line(ax, ay, bx, by, framebuffer, blue);
    line(cx, cy, bx, by, framebuffer, green);
    line(ax, ay, cx, cy, framebuffer, red);

    //在这张tga图片上设置这三个点并带上颜色
    framebuffer.set(ax, ay, red);
    framebuffer.set(bx, by, green);
    framebuffer.set(cx, cy, blue);

    //输出这张图片
    framebuffer.write_tga_file("framebuffer.tga");
    return 0;
}