#include "matrix/matrix.h"
#include "vec/Vec.h"
#include <cmath>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 角度制转弧度
static double toRadian(double angle){
    return angle * M_PI / 180.0;
}

mat<3, 3, float> matrix::matrix_33(const mat<3, 3, float>& lhs, const mat<3, 3, float>& rhs1){
    //3x3 矩阵乘法：直接复用 mat 的 operator*
    return lhs * rhs1;
}

// 3x3 矩阵的行列式：复用 mat::determinant()
float matrix::determinant(){
    return static_cast<float>(m.determinant());
}

mat<4, 4, float> matrix::matrix_44(const mat<4, 4, float>& lhs, const mat<4, 4, float>& rhs1){
    //4x4 矩阵乘法：直接复用 mat 的 operator*
    return lhs * rhs1;
}

mat<4, 4, double> matrix::translate(double tx, double ty, double tz){
    return mat<4, 4, double>{
        {1, 0, 0, tx},
        {0, 1, 0, ty},
        {0, 0, 1, tz},
        {0, 0, 0, 1}
    };
}

mat<4, 4, double> matrix::rotationX(double angle){
    const double a = toRadian(angle);
    const double c = std::cos(a);
    const double s = std::sin(a);

    return mat<4, 4, double>{
        {1, 0,  0, 0},
        {0, c, -s, 0},
        {0, s,  c, 0},
        {0, 0,  0, 1}
    };
}

mat<4, 4, double> matrix::rotationY(double angle){
    const double a = toRadian(angle);
    const double c = std::cos(a);
    const double s = std::sin(a);

    return mat<4, 4, double>{
        { c, 0, s, 0},
        { 0, 1, 0, 0},
        {-s, 0, c, 0},
        { 0, 0, 0, 1}
    };
}

mat<4, 4, double> matrix::rotationZ(double angle){
    const double a = toRadian(angle);
    const double c = std::cos(a);
    const double s = std::sin(a);

    return mat<4, 4, double>{
        {c, -s, 0, 0},
        {s,  c, 0, 0},
        {0,  0, 1, 0},
        {0,  0, 0, 1}
    };
}

mat<4, 4, double> matrix::scale(double sx, double sy, double sz){
    return mat<4, 4, double>{
        {sx, 0,  0,  0},
        {0,  sy, 0,  0},
        {0,  0,  sz, 0},
        {0,  0,  0,  1}
    };
}
