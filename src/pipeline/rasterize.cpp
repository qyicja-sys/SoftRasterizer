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

//计算边函数
// main.cpp 里也有同名函数，这里加 static 限定为本编译单元内部链接，避免重复定义
static int line_function(int Ax, int Ay, int Bx, int By, int x, int y) {
    return (Bx - Ax) * (y - Ay) - (By - Ay) * (x - Ax);
}

//计算三角形有向面积(平行四边形，需要除以二，这里没有除是方便其它使用)
static double signed_triangle_area(int x, int y, int bx, int by, int cx, int cy) {
    return .5*((by-y)*(bx+x) + (cy-by)*(cx+bx) + (y-cy)*(x+cx));
}

//三个点的叉积计算
static int cross_product(int ax, int ay, int bx, int by, int cx, int cy) {
    return (bx-ax)*(cy-ay) - (by-ay)*(cx-ax);
}

Vec4 rasterize::rasterizeGetClipVec(Vec4& vec){
    mat<4,4,double> modelView = TGAmodelView.getModelViewMatrix();
    mat<4,4,double> perspective = TGAperspective.getPerspectiveMatrix();
    return perspective * modelView * vec;
}

void rasterize::rasterizeInit(camera& camera, int width, int height){
    TGAmodelView.lookat(camera.getPosition(), camera.getCenter(), camera.getUp());

    TGAperspective.setCamera(camera);
    TGAperspective.perspectiveTransform(zBuffer, width, height);
    
    TGAviewPort.setWidth(width);
    TGAviewPort.setHeight(height);
    TGAviewPort.setViewPort(0, 0);
}

void rasterize::rasterizeSet(modelView modelView, perspective perspective, clipper clipper, viewPort viewPort, ZBuffer zBuffer){
    this->TGAmodelView = modelView;
    this->TGAperspective = perspective;
    this->TGAclipper = clipper;
    this->TGAviewPort = viewPort;
    this->zBuffer = zBuffer;
}



void rasterize::rasterize_colorMap(const vector<Vec4>& clip, TGAImage& framebuffer, const TGAColor color){
    // TODO: 实现颜色映射
    // mat<4,4,double> modelView = TGAmodelView.getModelViewMatrix();
    // mat<4,4,double> perspective = TGAperspective.getPerspectiveMatrix();
    mat<4,4,double> viewPort = TGAviewPort.getViewPortMatrix();
    Vec4 ndc[3] = {clip[0]/clip[0].w, clip[1]/clip[1].w, clip[2]/clip[2].w};
    Vec2 screen[3] = {(viewPort * ndc[0]).getScreenCoord(), (viewPort * ndc[1]).getScreenCoord(), (viewPort * ndc[2]).getScreenCoord()};

    mat<3,3> ABC = {{ {screen[0].x, screen[0].y, 1.}, {screen[1].x, screen[1].y, 1.}, {screen[2].x, screen[2].y, 1.} }};
    if (std::abs(ABC.determinant()) < 1) return; // 背面剔除 + 丢弃覆盖面积不足一个像素的三角形

    auto [bbminx,bbmaxx] = std::minmax({screen[0].x, screen[1].x, screen[2].x}); // 三角形的包围盒
    auto [bbminy,bbmaxy] = std::minmax({screen[0].y, screen[1].y, screen[2].y}); // 由左上角和右下角定义

    double triangle_area = signed_triangle_area(screen[0].x, screen[0].y, screen[1].x, screen[1].y, screen[2].x, screen[2].y);

#pragma omp parallel for
    for (int x=bbminx; x<=bbmaxx; x++) {
        for (int y=bbminy; y<=bbmaxy; y++) {
            double alpha = signed_triangle_area(x, y, screen[1].x, screen[1].y, screen[2].x, screen[2].y) / triangle_area;
            double beta = signed_triangle_area(x, y, screen[2].x, screen[2].y, screen[0].x, screen[0].y) / triangle_area;
            double gamma = signed_triangle_area(x, y, screen[0].x, screen[0].y, screen[1].x, screen[1].y) / triangle_area;
            const double eps = 1e-2 ;
            if (alpha < -eps || beta < -eps || gamma < -eps) continue ;// 重心坐标为负 => 该像素在三角形之外
            double N = zBuffer.getNearPlane();
            double F = zBuffer.getFarPlane();
            double A = (N+F)/(F-N);
            double B = -2*N*F/(F-N);
            double invW = alpha / clip[ 0 ].w + beta / clip[ 1 ].w + gamma / clip[ 2 ].w;
            double ndcZ = A + B * invW;
            unsigned char z = static_cast < unsigned char >(
                std:: clamp ((ndcZ + 1.0 ) * 0.5 * 254.0 , 0.0 , 254.0 ));
            if(!zBuffer.testAndSet(x, y, z)) continue;
            framebuffer.set(x, y, color);
        }
    }
}

void rasterize::rasterize_updateZBuffer(ZBuffer& zBuffer){
    this->zBuffer = zBuffer;
}

void rasterize::rasterize_updatePerspective(){
    TGAperspective.perspectiveTransform(zBuffer, TGAviewPort.getWidth(), TGAviewPort.getHeight());
}
