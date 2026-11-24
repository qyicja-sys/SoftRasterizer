#pragma once
#include <vector>
#include "vertex/vertex.h"
#include <string>
#include <array>
#include <cmath>
#include "vec/Vec.h"

struct Material {
    //RGB 三个颜色通道可以拥有不同的反射系数
    Vec3 Ka;       // 环境光反射系数
    Vec3 Kd;       // 漫反射系数
    Vec3 Ks;       // 镜面反射系数
    double Ns;      // 高光指数
};

struct Light {
    Vec3 position;       // 光源位置
    Vec3 ambient;        // 环境光强度
    Vec3 diffuse;        // 漫反射光强度
    Vec3 specular;       // 镜面反射光强度
};

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
    //存储所有顶点的位置、法线
    std::vector<Vertex> vertexList;
    // 所有纹理坐标，对应 OBJ 的 vt 行
    std::vector<Vec2> texList;
    // 所有三角形面，每个元素存 3 个顶点下标（已从 OBJ 的 1-based 转成 0-based）
    std::vector<std::array<int,3>> faceVertList;
    //所有所有三角形面，每个元素存 3 个顶点纹理下标
    std::vector<std::array<int,3>> faceTexList;
    // 所有三角形面，每个元素存 3 个顶点法线下标
    std::vector<std::array<int,3>> facenormalList;
    mat<4,4,double> modelMatrix;
    mat<3,3,double> normalMatrix;

    Material material;
    Light light;

    public:

        // 解析 OBJ 文件；打不开、格式错误或索引越界都返回 false
        bool load(const std::string& filename);

        // 按下标访问的接口内部用 vector::at，越界会抛 std::out_of_range
        // ---- 顶点 ----
        int nverts_V() const;// 顶点数量
        int nfaces_V() const;// 三角形面数量
        const Vec3& vertV(int i) const;// 按下标取顶点
        const std::array<int,3>& faceVert(int i) const;// 按下标取顶点面
        //array<int,3>顶点转vector<Vec3>
        std::vector<Vec3> faceVertVec(int i) const;

        // ---- 纹理坐标 ----
        int nverts_T() const;// 纹理坐标数量
        int nfaces_T() const;// 纹理三角形面数量
        const Vec2& vertT(int i) const;// 按下标取纹理坐标
        const std::array<int,3>& faceTex(int i) const;// 按下标取纹理面

        // ---- 法线 ----
        int nverts_N() const;// 法线顶点数量
        int nfaces_N() const;// 法线三角形面数量
        const Vec3& vertN(int i) const;// 按下标取法线
        const std::array<int,3>& faceNormal(int i) const;// 按下标取法线面

        // ---- 变换矩阵 ----
        mat<4,4,double> getModelMatrix() const;
        void setModelMatrix(Vec3 position, Vec3 rotation, Vec3 scale);
        mat<3,3,double> getNormalMatrix() const;
        void setNormalMatrix();

        // ---- 材质 ----
        Material getMaterial() const;// 获取材质
        void setMaterial(Vec3 Ka, Vec3 Kd, Vec3 Ks, double Ns);// 设置材质

        // ---- 光源 ----
        Light getLight() const;// 获取光源
        void setLight(Vec3 position, Vec3 ambient, Vec3 diffuse, Vec3 specular);// 设置光源
};
