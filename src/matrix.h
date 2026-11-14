#pragma once
#include <cstddef>
#include <initializer_list>

// 行列数用 std::size_t 更贴合数组下标，避免 int -> size_t 的隐式转换告警
template <std::size_t R, std::size_t C, typename T = double>
struct mat{
    T data[R][C];

    // 行列访问
    T&       operator()(std::size_t r, std::size_t c)       { return data[r][c]; }
    const T& operator()(std::size_t r, std::size_t c) const { return data[r][c]; }

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
