#pragma once
#include <cstddef>
#include <initializer_list>

// 行列数用 std::size_t 更贴合数组下标，避免 int -> size_t 的隐式转换告警
template <std::size_t R, std::size_t C, typename T = float>
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
};
