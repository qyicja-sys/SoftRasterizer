#include "zbuffer/zbuffer.h"
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

void ZBuffer::setNearPlane(double nearPlane){
    this->nearPlane = nearPlane;
}

void ZBuffer::setFarPlane(double farPlane){
    this->farPlane = farPlane;
}

float ZBuffer::getNearPlane() const{
    return nearPlane;
}

float ZBuffer::getFarPlane() const{
    return farPlane;
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
    // if(depth >= zBuffer[y * width + x]){
    //     return false;
    // }
    // zBuffer[y * width + x] = depth;
    // return true;
    // 光栅化循环用 OpenMP 并行执行，多个三角形对应的线程可能同时命中同一像素。
    // 普通“读-比较-写”会让两个线程都通过 depth < old 检查后互相覆盖，
    // 深度图/颜色图就会出现错乱的噪点。这里用原子 compare-exchange
    // 一次性完成比较与写入：只有 depth 严格小于当前值时才能写入。
    unsigned char* slot = &zBuffer[y * width + x];
    unsigned char old = __atomic_load_n(slot, __ATOMIC_RELAXED);
    while(depth < old){
        if(__atomic_compare_exchange_n(slot, &old, depth, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)){
            return true;
        }
        // CAS 失败时 old 已被更新为槽位当前值，回到循环重新比较
    }
    return false;
}
