#include "tgaimage.h"
#include "model.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <utility>

constexpr TGAColor white  = {255, 255, 255, 255};
constexpr TGAColor green  = {  0, 255,   0, 255};
constexpr TGAColor red    = {  0,   0, 255, 255};
constexpr TGAColor blue   = {255, 128,  64, 255};
constexpr TGAColor yellow = {  0, 200, 255, 255};

int main() {
    TGAImage framebuffer(200, 200, TGAImage::RGB);
    int ax =  7, ay =  3;
    int bx = 12, by = 100;
    TGAColor color = red;

    bool steep = std::abs(ax-bx) < std::abs(ay-by);
    if (steep) { // 如果线段较陡，就转置图像
        std::swap(ax, ay);
        std::swap(bx, by);
    }
    if (ax > bx) {
        std::swap(ax, bx); // 交换 x 坐标
        std::swap(ay, by); // 同时交换对应的 y 坐标，保持线段不变
    }

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
    framebuffer.write_tga_file("test.tga",false);
    return 0;
}