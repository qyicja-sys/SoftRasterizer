#include "tgaimage.h"
#include "model/model.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <utility>
#include "zbuffer/zbuffer.h"
#include "camera/camera.h"
#include "pipeline/modelview.h"
#include "pipeline/perspective.h"
#include "pipeline/clipper.h"
#include "pipeline/viewport.h"
#include "pipeline/rasterize.h"
#include "vertex/vertex.h"



// TGAColor 的通道类型是 std::uint8_t，取值范围 0~255。
// 写成 256 属于常量表达式中的窄化转换（ill-formed），会直接编译失败。
constexpr TGAColor white  = {255, 255, 255, 255};
constexpr TGAColor green  = {  0, 255,   0, 255};
constexpr TGAColor red    = {  0,   0, 255, 255};
constexpr TGAColor blue   = {255, 128,  64, 255};
constexpr TGAColor yellow = {  0, 200, 255, 255};

// 画布（帧缓冲）尺寸：800×800 像素。
// 下面的坐标映射会用到 width/2 和 height/2，改分辨率只需要改这两个常量。
constexpr int width  = 1024;
constexpr int height = 1024;
ZBuffer zBuffer(width, height);
rasterize TGArasterize;

struct randomShader : IShader {
    const Model &model;
    TGAColor color = {};
    Vec3 tri[3];

    randomShader(const Model &model) : model(model) {}

    virtual Vec4 vertex(const int vert) const {
        Vec3 v = model.vertV(vert);
        Vec4 v4 = v.toVec4();
        return TGArasterize.rasterizeGetClipVec(v4);
        //return v4;
    }
    
    virtual std::pair<bool,TGAColor> fragment(const Vec3 bar) const {
        return {false, color};                                    // 暂时处理
    }
};

struct phongShader : IShader {
    const Model &model;
    TGAColor color = {};

    phongShader(const Model &model) : model(model) {}

    virtual Vec3 calculateLight(camera& camera, Vec3 normalVec, Vec3 fragmentVec){
        Material material = model.getMaterial();
        Light light = model.getLight();
        normalVec = normalize(normalVec);

        // 全部用局部变量，避免并行光栅化时多线程写同一份 shader 成员（数据竞争）
        Vec3 Ambient = material.Ka.mul(light.ambient);

        Vec3 LightVec = normalize(light.position - fragmentVec);
        double diffuseFactor = std::max(0.0, normalVec*LightVec);
        Vec3 Diffuse = material.Kd.mul(light.diffuse) * diffuseFactor;

        // 只有光照正面（N·L > 0）时反射才有意义，否则跳过镜面项避免背面假高光
        Vec3 Specular(0, 0, 0);
        if (diffuseFactor > 0.0) {
            Vec3 ReflectVec = 2*normalVec*diffuseFactor - LightVec;
            Vec3 ViewVec = normalize(camera.getPosition() - fragmentVec);
            Specular = material.Ks.mul(light.specular)
                     * pow(std::max(0.0, ReflectVec*ViewVec), material.Ns);
        }
        return Ambient + Diffuse + Specular;
    }

    virtual Vec4 vertex(const int vert) const {
        Vec3 v = model.vertV(vert);
        Vec4 v4 = v.toVec4();
        return TGArasterize.rasterizeGetClipVec(v4);
        //return v4;
    }
    
    virtual std::pair<bool,TGAColor> fragment(const Vec3 bar) const {
        return {false, color};                                    // 暂时处理
    }
    
};

void Generate_LightAndMaterial(Model& model){
    model.setMaterial(Vec3(0.15, 0.15, 0.15), Vec3(0.8,  0.8,  0.8), Vec3(0.5,  0.5,  0.5), 32.0);
    model.setLight(Vec3(0, 1, 2), Vec3(0.15, 0.15, 0.15), Vec3(1.0, 1.0, 1.0), Vec3(1.0, 1.0, 1.0));
}

int main(int argc, char** argv){
    // 创建帧缓冲。TGAImage 构造函数会用默认颜色 TGAColor{}（全 0 = 黑色）
    // 填满整张图，所以不需要手动刷黑背景，画上去的线会覆盖对应像素。
    TGAImage framebuffer(width, height, TGAImage::RGB);
    TGAImage zMap(width, height, TGAImage::GRAYSCALE);

    zBuffer.clear();
    zBuffer.setNearPlane(std::numeric_limits< float >:: max ());
    zBuffer.setFarPlane(0);

    // modelView newModelView;
    // perspective newPerspective;
    // clipper newClipper;
    // viewPort newViewport;

    camera newCamera(500, width, height);
    // 相机必须在模型前方：worldToCamera 里 z 取反后，要求 position.z > 模型最大 z。
    // 放在原点等于站在模型内部，一半顶点 z_cam <= 0 被判为无效，
    // 另一半 z_cam 趋近 0，透视除法后坐标放大到屏幕外，最终仍是一片黑。
    newCamera.setPosition(Vec3(0, 0, 1));
    newCamera.setCenter(Vec3(0, 0, 0));
    newCamera.setUp(Vec3(0, 1, 0));
    newCamera.setFov();

    newCamera.setRotation(Vec3(15, 0, 0));//这里保存的是旋转坐标

    // 加载 OBJ 模型。路径是相对“当前工作目录”的：
    // VSCode 的 launch.json 把 cwd 设为项目根目录，所以能找到 obj/；
    // 如果直接在 build/bin 目录下启动 exe，会因找不到文件而加载失败。
    Model newModel;
    bool flag = newModel.load("obj/diablo3_pose.obj");
    Generate_LightAndMaterial(newModel);
    
    if(flag){
        // 只看数量不够——如果某个字段读错位，数量仍然可能对，但坐标会整体错掉。
        Vec3 firstVect = newModel.vertV(0);
        std::cout << "第一个顶点：" << firstVect.x << " " << firstVect.y <<  " " << firstVect.z << std::endl;

        // 统计模型在相机空间的深度范围，用于把深度归一化到 [0,254]。
        // 相机空间 z 本身只有 1.2~2.8 这种小范围，直接转 unsigned char 会被截断成 1/2，
        // 深度图就只剩两三个灰度级，看起来“不对”。
        Vec3 position = {0,0,0};
        Vec3 rotation = {0,0,0};
        Vec3 scale = {1,1,1};
        newModel.setModelMatrix(position, rotation, scale);
        newModel.setModelMatrix(position, rotation, scale);
        TGArasterize.rasterizeSetModelView(newCamera, newModel);          // 让 newModelView 真正算出来

        mat<4,4,double> mv = TGArasterize.getModelView().getModelViewMatrix();   // 提到循环外
        for (int i = 0; i < newModel.nverts_V(); i++) {
            float z = (mv * newModel.vertV(i).toVec4()).z;
            if (z > 0) {
                zBuffer.setNearPlane(std::min(zBuffer.getNearPlane(), z));
                zBuffer.setFarPlane (std::max(zBuffer.getFarPlane(),  z));
            }
        }

        TGArasterize.rasterizeSetZbuffer(zBuffer);
        TGArasterize.rasterizeSetModel(newModel);
        TGArasterize.rasterizeSetCamera(newCamera);
        //TGArasterize.rasterizeSet(newModelView, newPerspective, newClipper, newViewport, zBuffer, newModel);  // zBuffer 此时已是有效值
        TGArasterize.rasterizeInit(newCamera, width, height);

        // 遍历模型里的每个三角形面（本模型共 5022 个）。
        // face(i) 返回的是 3 个“顶点下标”，不是坐标；
        // 必须再用 vert(下标) 查表，才能拿到真正的 3D 坐标。
        for(int i=0;i<newModel.nfaces_V();i++){
            phongShader shader(newModel);
            const std::array<int,3> modelFace = newModel.faceVert(i);// 第 i 个面的 3 个顶点下标
            const std::array<int,3> modelNormalFace = newModel.faceNormal(i);// 第 i 个面的 3 个法线下标

            // 先收集这个三角形的 3 个屏幕点，再统一画线。
            // 这里创建空 vector 后 push 三次；不能写成 vector(3) 再 push，
            // 那样会先得到 3 个默认元素，再加 3 个新元素，共 6 个。
            // std::vector<Vec4> clip;
            // clip.reserve(3);

            // std:vector<Vec3> normal;
            // normal.reserve(3);

            std::vector<Vertex> clip;
            

            // 遍历三角形的 3 个顶点，将它们转换为屏幕坐标。
            for(int j = 0;j<3;j++){
                int index1 = modelFace.at(j);// 模型空间中的 3D 顶点
                int index2 = modelNormalFace.at(j);// 模型空间中的 3D 法线
                Vec3 world = (newModel. getModelMatrix () * newModel. vertV (index1). toVec4 ()). toVec3 ();
                Vertex v = {shader. vertex (index1), newModel. vertN (index2), world};
                clip.push_back(v);
            }
            clipper clipPolygon;
            clipPolygon.isClipped(clip);
            vector<vector<Vertex>> ready_rasterize_face = clipPolygon.triangulation();
            
            
            // 画三角形的三条边：0-1、0-2、1-2，首尾相连正好闭合。
            for(int i=0;i<ready_rasterize_face.size();i++){
                shader.color = { std::rand()%255, std::rand()%255, std::rand()%255, 255 };
                TGArasterize.rasterize_colorMap(ready_rasterize_face[i], shader, framebuffer);
                //TGArasterize.rasterize_depthMap(ready_rasterize_face[i], shader, zMap);
            }
        }

        // 输出 TGA。第二个参数 vflip 必须显式传 false：
        // write_tga_file 默认 vflip=true，会把文件头声明成“左下角为原点”，
        // 看图软件于是把内存第 0 行画在画面底部，相当于又上下翻了一次。
        // 上面的映射已经把“内存第 0 行 = 画面顶部”对齐好了，
        // 所以这里声明“左上角为原点”（vflip=false），避免翻转两次。
        framebuffer.write_tga_file("out/diablo3_pose_color.tga", false);
        //zMap.write_tga_file("out/diablo3_pose_depth.tga", false);
        zBuffer.clear();
    }else{
        std::cerr << "模型加载失败" << std::endl;
    }

    return 0;
}
