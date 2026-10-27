#pragma once
#include <vector>
#include <string>
#include <array>

// 模型空间中的三维点：x、y、z 对应 OBJ 文件 v 行的三个数。
// 本轮只做线框，z 暂时不参与绘制，但先原样保留，后面做深度测试要用。
struct Vec3{
    float x;
    float y;
    float z;

    Vec3(float x,float y, float z) : x(x), y(y), z(z) {}
};

// 完成“模型空间 -> 屏幕空间”映射后的顶点。
// x、y 是像素坐标（整数）；z 仍是模型空间的深度值，留到 z-buffer 阶段使用。
struct screenVert
{
    int x;
    int y;
    float z;

    screenVert(int x,int y, float z) : x(x), y(y), z(z) {}
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
