#include "matrix/matrix.h"
#include "vec/Vec.h"
#include <iostream>

mat<3, 3, float> matrix::matrix_33(const mat<3, 3, float>& lhs, const mat<3, 3, float>& rhs1){
    //3x3 矩阵乘法：直接复用 mat 的 operator*
    return lhs * rhs1;
}
