#pragma once
#include <cmath>
#include <cassert>
#include <iostream>

struct Vec2{
    float x;
    float y;

    Vec2(float x,float y) : x(x), y(y) {}

    // 向量的加减法
    Vec2 operator+(const Vec2& other) const {
        return Vec2(x + other.x, y + other.y);
    }
    Vec2 operator-(const Vec2& other) const {
        return Vec2(x - other.x, y - other.y);
    }
    // 向量的点积
       double operator*(const Vec2& other) const {
        return x * other.x + y * other.y;
    }
    // 向量的模长
    double magnitude() const {
        return std::sqrt(x * x + y * y);
    }
};

// 模型空间中的三维点：x、y、z 对应 OBJ 文件 v 行的三个数。
// 本轮只做线框，z 暂时不参与绘制，但先原样保留，后面做深度测试要用。
struct Vec3{
    float x;
    float y;
    float z;

    Vec3(float x,float y, float z) : x(x), y(y), z(z) {}
    // 向量的加减法
    Vec3 operator+(const Vec3& other) const {
        return Vec3(x + other.x, y + other.y, z + other.z);
    }
    Vec3 operator-(const Vec3& other) const {
        return Vec3(x - other.x, y - other.y, z - other.z);
    }
    // 向量的点积
       double operator*(const Vec3& other) const {
        return x * other.x + y * other.y + z * other.z;
    }
    // 向量的模长
    double magnitude() const {
        return std::sqrt(x * x + y * y + z * z);
    }
};

struct Vec4{
    float x;
    float y;
    float z;
    float w;

    Vec4(float x,float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    // 向量的加减法
    Vec4 operator+(const Vec4& other) const {
        return Vec4(x + other.x, y + other.y, z + other.z, w + other.w);
    }
    Vec4 operator-(const Vec4& other) const {
        return Vec4(x - other.x, y - other.y, z - other.z, w - other.w);
    }
    // 向量的点积
       double operator*(const Vec4& other) const {
        return x * other.x + y * other.y + z * other.z + w * other.w;
    }
    // 向量的模长
    double magnitude() const {
        return std::sqrt(x * x + y * y + z * z + w * w);
    }
};