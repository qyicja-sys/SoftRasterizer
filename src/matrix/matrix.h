#pragma once
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <utility>

// 行列数用 std::size_t 更贴合数组下标，避免 int -> size_t 的隐式转换告警
template <std::size_t R, std::size_t C, typename T = double>
struct mat{
    T data[R][C];

    // 行列访问
    T&       operator()(std::size_t r, std::size_t c)       { return data[r][c]; }
    const T& operator()(std::size_t r, std::size_t c) const { return data[r][c]; }

    // 默认构造：全部元素置零，方便先声明后赋值
    mat() : data{} {}

    //构造函数
    mat(std::initializer_list<std::initializer_list<T>> values) {
        std::size_t r = 0;

        for (const auto& row : values) {
            std::size_t c = 0;

            for (const auto& value : row) {
                data[r][c++] = value;
            }

            ++r;
        }
    }

    // 方阵行列式：高斯消元 + 部分主元选择
    T determinant() const {
        static_assert(R == C, "determinant() 仅支持方阵");

        T a[R][C];
        for (std::size_t r = 0; r < R; ++r) {
            for (std::size_t c = 0; c < C; ++c) {
                a[r][c] = data[r][c];
            }
        }

        T det = T(1);
        for (std::size_t col = 0; col < R; ++col) {
            // 在当前列中选取绝对值最大的元素作为主元，提高数值稳定性
            std::size_t pivot = col;
            for (std::size_t r = col + 1; r < R; ++r) {
                if (std::abs(a[r][col]) > std::abs(a[pivot][col])) {
                    pivot = r;
                }
            }

            // 主元为 0 说明矩阵奇异，行列式为 0
            if (a[pivot][col] == T(0)) {
                return T(0);
            }

            // 行交换会使行列式符号取反
            if (pivot != col) {
                for (std::size_t c = 0; c < C; ++c) {
                    std::swap(a[col][c], a[pivot][c]);
                }
                det = -det;
            }

            det *= a[col][col];

            // 消去当前列下方的元素
            for (std::size_t r = col + 1; r < R; ++r) {
                const T factor = a[r][col] / a[col][col];
                for (std::size_t c = col; c < C; ++c) {
                    a[r][c] -= factor * a[col][c];
                }
            }
        }
        return det;
    }
};

// 矩阵乘法运算符：R x K 乘 K x C，得到 R x C
// 要求左矩阵的列数 K 与右矩阵的行数 K 一致，否则模板参数推导失败
template <std::size_t R, std::size_t K, std::size_t C, typename T>
mat<R, C, T> operator*(const mat<R, K, T>& lhs, const mat<K, C, T>& rhs) {
    mat<R, C, T> result;
    for (std::size_t r = 0; r < R; ++r) {
        for (std::size_t c = 0; c < C; ++c) {
            for (std::size_t k = 0; k < K; ++k) {
                result(r, c) += lhs(r, k) * rhs(k, c);
            }
        }
    }
    return result;
}

class matrix{
    private:
        mat<3, 3> m;
        
    public:
        matrix(mat<3, 3> m) : m(m) {}

        // 矩阵乘法
        mat<3, 3, float> matrix_33(const mat<3, 3, float>& lhs, const mat<3, 3, float>& rhs1);
        mat<4, 4, float> matrix_44(const mat<4, 4, float>& lhs, const mat<4, 4, float>& rhs1);

        float determinant();
};
