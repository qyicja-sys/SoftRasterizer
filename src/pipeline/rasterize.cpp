#include "rasterize.h"
#include "matrix/matrix.h"
#include "camera/camera.h"
#include "clipper.h"
#include "modelView.h"
#include "viewPort.h"
#include "perspective.h"
#include "zbuffer/zbuffer.h"
#include "model/model.h"
#include "vec/Vec.h"
#include <algorithm>
#include <iostream>
#include <vector>
#include <cmath>
#include "texture/Texture.h"

//计算边函数
double line_function(double Ax, double Ay, double Bx, double By, double x, double y) {
    return (Bx - Ax) * (y - Ay) - (By - Ay) * (x - Ax);
}

//计算三角形有向面积(平行四边形，需要除以二，这里没有除是方便其它使用)
// 参数必须用 double：屏幕坐标是 double，若用 int 会截断，
// 边长不足 1 像素的细长三角形截断后三点重合，面积为 0，
// 重心坐标出现 0/0=NaN，深度被强转成 0，表现为深度图上的白色噪点。
static double signed_triangle_area(double x, double y, double bx, double by, double cx, double cy) {
    return .5*((by-y)*(bx+x) + (cy-by)*(cx+bx) + (y-cy)*(x+cx));
}

//三个点的叉积计算
static int cross_product(int ax, int ay, int bx, int by, int cx, int cy) {
    return (bx-ax)*(cy-ay) - (by-ay)*(cx-ax);
}

//判断边是否为顶部左侧
// 注意：屏幕坐标原点在左上角、y 轴向下；且 line_function 调用时把 (x,y) 放在了前两位，
// 使边函数符号与常规 cross(B-A, P-A) 相反（三角形内部点满足 E > 0，见 insideEdge）。
// 在该符号约定下推导：
//   水平边 dy == 0：E = (Y-Py)(Bx-Ax)，内部在下方(Py>Y)要求 dx < 0  => top edge
//   垂直边 dx == 0：E = (X-Px)(Ay-By)，内部在右侧(Px>X)要求 dy > 0  => left edge
// 因此 top-left 的正确条件应是 dy > 0 或 (dy == 0 && dx < 0)，与常见 y 向上写法相反。
bool isTopLeft(Vec2& A, Vec2& B){
    double dx = B.x - A.x;
    double dy = B.y - A.y;
    return (dy < 0) || (dy == 0 && dx > 0);
}

//判断点是否在边内
bool insideEdge(double E, bool topLeft)
{
    if (E < 0)
        return false;

    if (topLeft){
        if(E>=0)
            return true;
        else
            return false;
    }else{
        if (E > 0)
            return true;
    }

    return topLeft;
    //return (E > 0) || (E == 0 && topLeft);
}

Vec4 rasterize::rasterizeGetClipVec(Vec4& vec){
    mat<4,4,double> modelView = TGAmodelView.getModelViewMatrix();
    mat<4,4,double> perspective = TGAperspective.getPerspectiveMatrix();
    return perspective * modelView * vec;
}

void rasterize::rasterizeInit(camera& camera, int width, int height){
    TGAmodelView.lookat(camera, m);

    TGAperspective.setCamera(camera);
    TGAperspective.perspectiveTransform(zBuffer, width, height);
    
    TGAviewPort.setWidth(width);
    TGAviewPort.setHeight(height);
    TGAviewPort.setViewPort();
}

void rasterize::rasterizeSet(camera& camera, modelView modelView, perspective perspective, clipper clipper, viewPort viewPort, ZBuffer zBuffer, Model& model){
    this->TGAcamera = camera;
    this->TGAmodelView = modelView;
    this->TGAperspective = perspective;
    this->TGAclipper = clipper;
    this->TGAviewPort = viewPort;
    this->zBuffer = zBuffer;
    this->m = model;
}

void rasterize::rasterizeSetZbuffer(ZBuffer zBuffer){
    this->zBuffer = zBuffer;
}

void rasterize::rasterizeSetModel(Model& model){
    this->m = model;
}

void rasterize::rasterizeSetModelView(camera& camera, Model& model){
    this->TGAmodelView.lookat(camera, model);
}

modelView rasterize::getModelView() const{
    return TGAmodelView;
}

void rasterize::rasterizeSetCamera(camera& camera){
    this->TGAcamera = camera;
}


//单独初始化modelView
void rasterize::initModelView(camera& camera, Model& model){
    TGAmodelView.lookat(camera, model);
}

//获取modelView矩阵
mat<4,4,double> rasterize::getModelViewMatrix() const{
    return TGAmodelView.getModelViewMatrix();
}


void rasterize::rasterize_colorMap(const vector<Vertex>& clip, IShader& shader, Texture& texture, TGAImage& framebuffer){
    // TODO: 实现颜色映射
    // mat<4,4,double> modelView = TGAmodelView.getModelViewMatrix();
    // mat<4,4,double> perspective = TGAperspective.getPerspectiveMatrix();
    mat<4,4,double> viewPort = TGAviewPort.getViewPortMatrix();
    Vec4 frag_p[clip.size()];
    Vec3 frag_n[clip.size()];
    Vec3 frag_w[clip.size()];
    for (int i = 0; i < clip.size(); i++){
        frag_p[i] = clip[i].position;
        frag_n[i] = clip[i].normal;
        frag_w[i] = clip[i].worldPos;
    }
    
    Vec4 ndc[3] = {frag_p[0]/frag_p[0].w, frag_p[1]/frag_p[1].w, frag_p[2]/frag_p[2].w};
    Vec2 screen[3] = {(viewPort * ndc[0]).getScreenCoord(), (viewPort * ndc[1]).getScreenCoord(), (viewPort * ndc[2]).getScreenCoord()};
    double w[3] = {frag_p[0].w, frag_p[1].w, frag_p[2].w};

    mat<3,3> ABC = {{ {screen[0].x, screen[0].y, 1.}, {screen[1].x, screen[1].y, 1.}, {screen[2].x, screen[2].y, 1.} }};
    // 背面剔除：屏幕 y 轴向下，模型正面在屏幕上呈顺时针，对应 determinant < 0，
    // determinant > 0 的是背面，直接丢弃。
    if (ABC.determinant() > -1) return;

    // 统一绕向：交换 s1、s2 后，正面三角形在屏幕空间变为逆时针（内部点 E > 0），
    // insideEdge 的 E > 0 判断和 top-left 规则才能对正面生效。
    std::swap(screen[1], screen[2]);
    std::swap(w[1], w[2]);
    std:: swap (frag_n[ 1 ], frag_n[ 2 ]);
    std:: swap (frag_w[ 1 ], frag_w[ 2 ]);

    auto [bbminx,bbmaxx] = std::minmax({screen[0].x, screen[1].x, screen[2].x}); // 三角形的包围盒
    auto [bbminy,bbmaxy] = std::minmax({screen[0].y, screen[1].y, screen[2].y}); // 由左上角和右下角定义

    double triangle_area = signed_triangle_area(screen[0].x, screen[0].y, screen[1].x, screen[1].y, screen[2].x, screen[2].y);

    bool istopleft1 = isTopLeft(screen[0], screen[1]);
    bool istopleft2 = isTopLeft(screen[1], screen[2]);
    bool istopleft3 = isTopLeft(screen[2], screen[0]);

#pragma omp parallel for
    for (int x=bbminx; x<=bbmaxx; x++) {
        for (int y=bbminy; y<=bbmaxy; y++) {
            double E0 = line_function(x, y, screen[0].x, screen[0].y, screen[1].x, screen[1].y);
            double E1 = line_function(x, y, screen[1].x, screen[1].y, screen[2].x, screen[2].y);
            double E2 = line_function(x, y, screen[2].x, screen[2].y, screen[0].x, screen[0].y);
            bool flag = insideEdge(E0, istopleft1) &&
            insideEdge(E1, istopleft2) &&
            insideEdge(E2, istopleft3);
            if (!flag) continue;

            double alpha = signed_triangle_area(x, y, screen[1].x, screen[1].y, screen[2].x, screen[2].y) / triangle_area;
            double beta = signed_triangle_area(x, y, screen[2].x, screen[2].y, screen[0].x, screen[0].y) / triangle_area;
            double gamma = signed_triangle_area(x, y, screen[0].x, screen[0].y, screen[1].x, screen[1].y) / triangle_area;
            Vec3 bc = {alpha, beta, gamma};
            
            double N = zBuffer.getNearPlane();
            double F = zBuffer.getFarPlane();
            double A = (N+F)/(F-N);
            double B = -2*N*F/(F-N);
            double invW = alpha / w[0] + beta / w[1] + gamma / w[2];
            double ndcZ = A + B * invW;
            unsigned char z = static_cast < unsigned char >(
                std:: clamp ((ndcZ + 1.0 ) * 0.5 * 254.0 , 0.0 , 254.0 ));
            if(!zBuffer.testAndSet(x, y, z)) continue;

            auto [discard, color] = shader.fragment(bc);
            if (discard) continue;

            // 透视校正插值：世界坐标
            Vec3 fragWorld = (alpha * frag_w[ 0 ] / w[ 0 ]
                             + beta  * frag_w[ 1 ] / w[ 1 ]
                             + gamma * frag_w[ 2 ] / w[ 2 ]) / invW;
            
            // 法线也顺便做透视校正
            Vec3 Np = normalize ((alpha * frag_n[ 0 ] / w[ 0 ]
                                + beta  * frag_n[ 1 ] / w[ 1 ]
                                + gamma * frag_n[ 2 ] / w[ 2 ]) / invW);
            
            // 纹理坐标也顺便做透视校正
            Vec2 fragTextureUV = (alpha * clip[0].textureUV / w[0 ]
                                + beta  * clip[1].textureUV / w[1 ]
                                + gamma * clip[2].textureUV / w[2 ]) / invW;
            
            TGAColor fragColor = texture.sample(fragTextureUV);

            Vec3 lightColor = shader. calculateLight (TGAcamera, Np, fragWorld);
            // 逐通道乘光照颜色，并裁剪到 [0,255]（bgra: 0=B, 1=G, 2=R）
            auto toByte = [](double v){ return static_cast<unsigned char>(std::clamp(v, 0.0, 255.0)); };
            TGAColor finalColor = {
                toByte(fragColor.bgra[0] * lightColor.z),
                toByte(fragColor.bgra[1] * lightColor.y),
                toByte(fragColor.bgra[2] * lightColor.x),
                fragColor.bgra[3]
            };
            framebuffer.set(x, y, finalColor);
        }
    }
}

void rasterize::rasterize_updateZBuffer(ZBuffer& zBuffer){
    this->zBuffer = zBuffer;
}

void rasterize::rasterize_updatePerspective(){
    TGAperspective.perspectiveTransform(zBuffer, TGAviewPort.getWidth(), TGAviewPort.getHeight());
}
