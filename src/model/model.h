#pragma once
#include <vector>
#include <string>
#include <array>
#include <cmath>
#include "vec/Vec.h"



// 完成“模型空间 -> 屏幕空间”映射后的顶点。
// x、y 是像素坐标（整数）；z 仍是模型空间的深度值，留到 z-buffer 阶段使用。
struct screenVert
{
    int x;
    int y;
    float z;
    bool valid;

    screenVert(int x,int y, float z, bool valid) : x(x), y(y), z(z), valid(valid) {}
};

class Model{
    // 所有顶点，对应 OBJ 的 v 行
    std::vector<Vec3> vecList;
    // 所有三角形面，每个元素存 3 个顶点下标（已从 OBJ 的 1-based 转成 0-based）
    std::vector<std::array<int,3>> faceList;

    public:
        // 解析 OBJ 文件；打不开、格式错误或索引越界都返回 false
        bool load(const std::string& filename);

        int nverts() const;// 顶点数量
        int nfaces() const;// 三角形面数量

        // 按下标取顶点 / 面。内部用 vector::at，越界会抛 std::out_of_range。
        const Vec3& vert(int i) const;
        const std::array<int,3>& face(int i) const;
};
