#pragma once
#include <cmath>
#include <cassert>
#include <iostream>
#include <vector>
#include "matrix.h"

// 严格 -std=c++17 下 <cmath> 不暴露 M_PI，这里兜底定义；已有定义时不重复定义
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif




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

// 前置声明：Vec3 的旋转函数里要用到 mat * Vec3，运算符必须在结构体定义之前可见
struct Vec3;
inline Vec3 operator*(const mat<3,3>& m, const Vec3& v);

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

// 三阶矩阵乘向量（放在 Vec3 定义之后，避免 matrix.h 反向包含 Vec.h）
inline Vec3 operator*(const mat<3,3>& m, const Vec3& v) {
    return Vec3(m(0,0)*v.x + m(0,1)*v.y + m(0,2)*v.z,
                m(1,0)*v.x + m(1,1)*v.y + m(1,2)*v.z,
                m(2,0)*v.x + m(2,1)*v.y + m(2,2)*v.z);
}

// 绕坐标轴旋转，angle 单位为度。这些函数不依赖 this，做成自由函数便于在 main 里直接调用
inline Vec3 rotX(const Vec3& v, double angle) {
    double a = angle*M_PI/180.0;
    mat<3,3> Rx{{1,0,0}, {0,std::cos(a),-std::sin(a)}, {0,std::sin(a),std::cos(a)}};
    return Rx*v;
}

inline Vec3 rotY(const Vec3& v, double angle) {
    double a = angle*M_PI/180.0;
    mat<3,3> Ry{{{std::cos(a), 0, std::sin(a)}, {0,1,0}, {-std::sin(a), 0, std::cos(a)}}};
    return Ry*v;
}

inline Vec3 rotZ(const Vec3& v, double angle) {
    double a = angle*M_PI/180.0;
    mat<3,3> Rz{{{std::cos(a),-std::sin(a),0}, {std::sin(a),std::cos(a),0}, {0,0,1}}};
    return Rz*v;
}

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