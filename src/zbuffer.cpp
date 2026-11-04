#include "zbuffer.h"
#include <limits>
#include <vector>

// 深度缓冲初值取 unsigned char 的最大值（255），表示“最远”，任何真实像素深度都比它小
namespace {
constexpr unsigned char kZBufferInit = std::numeric_limits<unsigned char>::max();
}

ZBuffer::ZBuffer(int width, int height)
    : width(width), height(height), zBuffer(width * height, kZBufferInit)
{
}

ZBuffer::~ZBuffer()
{
}

void ZBuffer::clear(){
    zBuffer.assign(width * height, kZBufferInit);
}

unsigned char ZBuffer::get(int x, int y) const{
    // 屏幕外的像素没有深度值，返回“最远”的初值
    if(x < 0 || x >= width || y < 0 || y >= height){
        return kZBufferInit;
    }
    return zBuffer[y * width + x];
}

bool ZBuffer::testAndSet(int x, int y, unsigned char depth){
    // 三角形顶点投影后可能落在屏幕外（如 x==1024），包围盒会越界；
    // 这里必须先挡住，否则 unsigned 下标回绕成巨大值，vector 越界崩溃。
    if(x < 0 || x >= width || y < 0 || y >= height){
        return false;
    }
    if(depth >= zBuffer[y * width + x]){
        return false;
    }
    zBuffer[y * width + x] = depth;
    return true;
}
