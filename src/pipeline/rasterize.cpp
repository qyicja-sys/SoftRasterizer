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
int line_function(int Ax, int Ay, int Bx, int By, int x, int y) {
    return (Bx - Ax) * (y - Ay) - (By - Ay) * (x - Ax);
}

//计算三角形有向面积(平行四边形，需要除以二，这里没有除是方便其它使用)
double signed_triangle_area(int x, int y, int bx, int by, int cx, int cy) {
    return .5*((by-y)*(bx+x) + (cy-by)*(cx+bx) + (y-cy)*(x+cx));
}

//三个点的叉积计算
int cross_product(int ax, int ay, int bx, int by, int cx, int cy) {
    return (bx-ax)*(cy-ay) - (by-ay)*(cx-ax);
}

void rasterize::rasterizeInit(camera& camera, int width, int height){
    TGAmodelView.lookat(camera.getPosition(), camera.getCenter(), camera.getUp());

    TGAperspective.setCamera(camera);
    TGAperspective.perspectiveTransform(zBuffer);
    
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
    if (ABC.determinant()<1) return; // 背面剔除 + 丢弃覆盖面积不足一个像素的三角形

    auto [bbminx,bbmaxx] = std::minmax({screen[0].x, screen[1].x, screen[2].x}); // 三角形的包围盒
    auto [bbminy,bbmaxy] = std::minmax({screen[0].y, screen[1].y, screen[2].y}); // 由左上角和右下角定义

    double triangle_area = signed_triangle_area(screen[0].x, screen[0].y, screen[1].x, screen[1].y, screen[2].x, screen[2].y);

    double alpha_area_start =signed_triangle_area(bbminx, bbminy, screen[1].x, screen[1].y, screen[2].x, screen[2].y);
    double beta_area_start =signed_triangle_area(bbminx, bbminy, screen[2].x, screen[2].y, screen[0].x, screen[0].y);
    double gamma_area_start =signed_triangle_area(bbminx, bbminy, screen[0].x, screen[0].y, screen[1].x, screen[1].y);

    //X方向的增量
    double a1 = (screen[1].y - screen[2].y)*.5;
    double a2 = (screen[2].y - screen[0].y)*.5;
    double a3 = (screen[0].y - screen[1].y)*.5;

    //Y方向的增量
    double b1 = (screen[2].x - screen[1].x)*.5;
    double b2 = (screen[0].x - screen[2].x)*.5;
    double b3 = (screen[1].x - screen[0].x)*.5;
    

#pragma omp parallel for
    for (int x=bbminx; x<=bbmaxx; x++) {
        double alpha_init = alpha_area_start + (x - bbminx)*a1;
        double beta_init = beta_area_start + (x - bbminx)*a2;
        double gamma_init = gamma_area_start + (x - bbminx)*a3;
        for (int y=bbminy; y<=bbmaxy; y++) {
            double alpha = alpha_init / triangle_area;
            double beta  = beta_init / triangle_area;
            double gamma = gamma_init / triangle_area;
            alpha_init += b1;
            beta_init += b2;
            gamma_init += b3;
            if (alpha<0 || beta<0 || gamma<0) continue; // 重心坐标为负 => 该像素在三角形之外
            unsigned char z = static_cast<unsigned char>(alpha * ndc[0].z + beta * ndc[1].z + gamma * ndc[2].z);
            if(!zBuffer.testAndSet(x, y, z)) continue;
            framebuffer.set(x, y, color);
        }
    }
}
