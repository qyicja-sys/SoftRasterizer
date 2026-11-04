#include "tgaimage.h"
#include "model.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <utility>

// TGAColor 的通道类型是 std::uint8_t，取值范围 0~255。
// 写成 256 属于常量表达式中的窄化转换（ill-formed），会直接编译失败。
constexpr TGAColor white  = {255, 255, 255, 255};
constexpr TGAColor green  = {  0, 255,   0, 255};
constexpr TGAColor red    = {  0,   0, 255, 255};
constexpr TGAColor blue   = {255, 128,  64, 255};
constexpr TGAColor yellow = {  0, 200, 255, 255};

//按重心坐标((1-t),t)权重，来进行绘制，由近到远(A-B)
void line(int ax, int ay, int bx, int by, TGAImage &framebuffer, TGAColor color) {
    // for (float t=0.; t<1.; t+=.01) {
    //     int x = std::round( ax + (bx-ax)*t );
    //     int y = std::round( ay + (by-ay)*t );
    //     framebuffer.set(x, y, color);
    // }
    // 如果起点在终点的右边，就把它们对调一下
    // 如果线比较陡（y 跨度大于 x 跨度），我们以 y 为主步进变量
    bool steep = std::abs(ax-bx) < std::abs(ay-by);
    if (steep) { // 如果线段较陡，就转置图像
        std::swap(ax, ay);
        std::swap(bx, by);
    }

    if (ax > bx) {
        std::swap(ax, bx); // 交换 x 坐标
        std::swap(ay, by); // 同时交换对应的 y 坐标，保持线段不变
    }

    //最简单的运算
    // for (int x=ax; x<=bx; x++) {
    //     float t = (x-ax) / static_cast<float>(bx-ax);//计算当前点占整条线段的比例
    //     int y = std::round( ay + (by-ay)*t );
    //     if (steep) // 如果之前转置过，就转置回来
    //         framebuffer.set(y, x, color);
    //     else
    //         framebuffer.set(x, y, color);
    // }

    //优化1
    // float y = ay;
    // for (int x=ax; x<=bx; x++) {
    //     if (steep) // 如果之前转置过，就转置回来
    //         framebuffer.set(y, x, color);
    //     else
    //         framebuffer.set(x, y, color);
    //     y += (by-ay) / static_cast<float>(bx-ax);//斜率
    // }

    //浮点数乘法和强制类型转换在底层非常消耗 CPU 性能，所以需要进一步优化
    //优化2

    // int y = ay;
    // float error = 0;//用来计算斜率
    // for (int x=ax; x<=bx; x++) {
    //     if (steep) // 如果之前转置过，就转置回来
    //         framebuffer.set(y, x, color);
    //     else
    //         framebuffer.set(x, y, color);
    //     error += std::abs(by-ay)/static_cast<float>(bx-ax);
    //     if (error>.5) {
    //         y += by > ay ? 1 : -1;//决定走向，向下还是向上
    //         error -= 1.;//把已经兑现的误差抵消掉，保留多扣除的部分。如果不减去1，error 会一直累加，越来越大，y 就会疯狂地 +1
    //     }
    // }

    //优化3
    //消除浮点运算，采用Bresenham 画线算法（或者叫全整数误差法）
    int y = ay;
    int ierror = 0;
    for (int x=ax; x<=bx; x++) {
        if (steep) // 如果之前转置过，就转置回来
            framebuffer.set(y, x, color);
        else
            framebuffer.set(x, y, color);
        ierror += 2 * std::abs(by-ay);//原error += abs(by - ay) / (bx - ax)式子两边同时乘以Δx(bx - ax)，消除除法。实际就是对优化2的式子进行等价交换
        // if (ierror > bx - ax) {//同理
        //     y += by > ay ? 1 : -1;
        //     ierror -= 2 * (bx-ax);//同理
        // }
        //进一步优化，if判断会拖慢性能
        y += (by > ay ? 1 : -1) * (ierror > bx - ax);
        ierror -= 2 * (bx-ax)   * (ierror > bx - ax);
    }
}

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

//绘制三角形
//扫描线填充(我自己想的直线方程反函数填充，可以优化)
void triangle_1(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L<=0) return; // 背面
    // if (total_area<1) return; // 背面剔除 + 丢弃覆盖面积不足一个像素的三角形

    int min_y = std::min({ay, by, cy});
    int max_y = std::max({ay, by, cy});

    // 3. 逐行扫描，原先为一元函数，现在改利用反函数，填补缺失的像素点
    for (int y = min_y; y <= max_y; ++y) {
        std::vector<float> intersect_x;

        // 遍历三条边，分别求扫描线与边的交点
        auto add_intersection = [&](int x1, int y1, int x2, int y2) {
            // 水平边不参与交点计算
            if (y1 == y2) {
                return;
            }

            // 统一采用左闭右开式的 y 范围
            // 保证顶点不会被相邻两条边重复计算
            int low_y = std::min(y1, y2);
            int high_y = std::max(y1, y2);

            if (y >= low_y && y < high_y) {
                //线段参数函数，t 表示「从起点走到终点的比例」
                float t = static_cast<float>(y - y1) / (y2 - y1);
                float x = x1 + t * (x2 - x1);
                intersect_x.push_back(x);
            }
        };

        //判断扫描线落在哪两条边，直接无脑三次就不用判断具体是哪两条了
        add_intersection(ax, ay, bx, by);
        add_intersection(bx, by, cx, cy);
        add_intersection(ax, ay, cx, cy);

        // 正常情况下，非顶点扫描行会得到两个交点
        if (intersect_x.size() != 2) {
            continue;
        }

        float left = std::min(intersect_x[0], intersect_x[1]);
        float right = std::max(intersect_x[0], intersect_x[1]);

        int x_start = static_cast<int>(std::ceil(left));
        int x_end = static_cast<int>(std::floor(right));

        // 直接填充像素，不再调用 Bresenham
        for (int x = x_start; x <= x_end; ++x) {
            // 替换成你项目中实际的像素写入接口
            framebuffer.set(x, y, color);
        }
    }
}

//绘制三角形(扫描线填充，采用线性插值)
void triangle_2(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    // 利用叉积进行背面剔除
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L<=0) return; // 背面
    // if (total_area<1) return; // 背面剔除 + 丢弃覆盖面积不足一个像素的三角形

    // 按 y 坐标升序排列顶点 a、b、c（冒泡排序，好耶！）
    if (ay>by) { std::swap(ax, bx); std::swap(ay, by); }
    if (ay>cy) { std::swap(ax, cx); std::swap(ay, cy); }
    if (by>cy) { std::swap(bx, cx); std::swap(by, cy); }
    int total_height = cy-ay;

    if (ay != by) { // 如果下半部分没有退化
        int segment_height = by - ay;
        for (int y=ay; y<=by; y++) { // 从 ay 到 by 扫描水平线
            int x1 = ax + ((cx - ax)*(y - ay)) / total_height;
            int x2 = ax + ((bx - ax)*(y - ay)) / segment_height;
            for (int x=std::min(x1,x2); x<std::max(x1,x2); x++)  // 画一条水平线
                framebuffer.set(x, y, color);
        }
    }
    if (by != cy) { // 如果上半部分没有退化
        int segment_height = cy - by;
        for (int y=by; y<=cy; y++) { // 从 by 到 cy 扫描水平线
            int x1 = ax + ((cx - ax)*(y - ay)) / total_height;
            int x2 = bx + ((cx - bx)*(y - by)) / segment_height;
            for (int x=std::min(x1,x2); x<std::max(x1,x2); x++)  // 画一条水平线
                framebuffer.set(x, y, color);
        }
    }
}

//绘制三角形(扫描线填充，边函数+单方向增量计算)
void triangle_3_1(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    int bbminx = std::min(std::min(ax, bx), cx); // 三角形的包围盒
    int bbminy = std::min(std::min(ay, by), cy); // 由左上角和右下角定义
    int bbmaxx = std::max(std::max(ax, bx), cx);
    int bbmaxy = std::max(std::max(ay, by), cy);
    double total_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
    // 利用叉积进行背面剔除
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L<=0) return; // 背面
    // if (total_area<1) return; // 背面剔除 + 丢弃覆盖面积不足一个像素的三角形

#pragma omp parallel for
    for(int y=bbminy; y<=bbmaxy; y++){
        int Ep1 = line_function(ax, ay, bx, by, bbminx, y);
        int Ep2 = line_function(bx, by, cx, cy, bbminx, y);
        int Ep3 = line_function(cx, cy, ax, ay, bbminx, y);
        
        int a1 = ay - by;
        int a2 = by - cy;
        int a3 = cy - ay;
        for(int x=bbminx; x<=bbmaxx; x++){
            if((Ep1>=0 && Ep2>=0 && Ep3>=0) || (Ep1<=0 && Ep2<=0 && Ep3<=0)){
                framebuffer.set(x, y, color);
            }
            Ep1 += a1;
            Ep2 += a2;
            Ep3 += a3;
        }
    }
}

//绘制三角形(扫描线填充，边函数+双方向增量计算)
void triangle_3(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    int bbminx = std::min(std::min(ax, bx), cx); // 三角形的包围盒
    int bbminy = std::min(std::min(ay, by), cy); // 由左上角和右下角定义
    int bbmaxx = std::max(std::max(ax, bx), cx);
    int bbmaxy = std::max(std::max(ay, by), cy);
    double total_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
    // 利用叉积进行背面剔除
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L<=0) return; // 背面
    // if (total_area<1) return; // 背面剔除 + 丢弃覆盖面积不足一个像素的三角形

    // 初始化边函数值，方便后续做增量计算
    int E1_row = line_function(ax, ay, bx, by, bbminx, bbminy);
    int E2_row = line_function(bx, by, cx, cy, bbminx, bbminy);
    int E3_row = line_function(cx, cy, ax, ay, bbminx, bbminy);

    //X方向的增量
    int a1 = ay - by;
    int a2 = by - cy;
    int a3 = cy - ay;

    //Y方向的增量
    int b1 = bx - ax;
    int b2 = cx - bx;
    int b3 = ax - cx;

#pragma omp parallel for
    for(int y=bbminy; y<=bbmaxy; y++){
        // 每一行独立初始化，每一行的初始边函数值应该独立计算，不能直接让上一行的变量状态影响下一行
        int Ep1 = E1_row + (y - bbminy) * b1;
        int Ep2 = E2_row + (y - bbminy) * b2;
        int Ep3 = E3_row + (y - bbminy) * b3;
        for(int x=bbminx; x<=bbmaxx; x++){
            if((Ep1>=0 && Ep2>=0 && Ep3>=0) || (Ep1<=0 && Ep2<=0 && Ep3<=0)){
                framebuffer.set(x, y, color);
            }
            Ep1 += a1;
            Ep2 += a2;
            Ep3 += a3;
        }
    }
}

//绘制三角形面积，利用重心坐标判断像素是否在三角形内
void triangle_4(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    int bbminx = std::min(std::min(ax, bx), cx); // 三角形的包围盒
    int bbminy = std::min(std::min(ay, by), cy); // 由左上角和右下角定义
    int bbmaxx = std::max(std::max(ax, bx), cx);
    int bbmaxy = std::max(std::max(ay, by), cy);
    double total_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
    // 利用叉积进行背面剔除
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L<=0) return; // 背面
    // if (total_area<1) return; // 背面剔除 + 丢弃覆盖面积不足一个像素的三角形

#pragma omp parallel for
    for (int x=bbminx; x<=bbmaxx; x++) {
        for (int y=bbminy; y<=bbmaxy; y++) {
            double alpha = signed_triangle_area(x, y, bx, by, cx, cy) / total_area;
            double beta  = signed_triangle_area(x, y, cx, cy, ax, ay) / total_area;
            double gamma = signed_triangle_area(x, y, ax, ay, bx, by) / total_area;
            if (alpha<0 || beta<0 || gamma<0) continue; // 重心坐标为负 => 该像素在三角形之外
            framebuffer.set(x, y, color);
        }
    }
}

//绘制三角形面积，利用重心坐标判断像素是否在三角形内(再修版，增加增量计算提升性能)
void triangle_5(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    // 利用叉积进行背面剔除
    int L = cross_product(ax, ay, bx, by, cx, cy);
    if(L<=0) return; // 背面
    // if (total_area<1) return; // 背面剔除 + 丢弃覆盖面积不足一个像素的三角形

    int bbminx = std::min(std::min(ax, bx), cx); // 三角形的包围盒
    int bbminy = std::min(std::min(ay, by), cy); // 由左上角和右下角定义
    int bbmaxx = std::max(std::max(ax, bx), cx);
    int bbmaxy = std::max(std::max(ay, by), cy);

    double triangle_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
    double alpha_area_start =signed_triangle_area(bbminx, bbminy, bx, by, cx, cy);
    double beta_area_start =signed_triangle_area(bbminx, bbminy, cx, cy, ax, ay);
    double gamma_area_start =signed_triangle_area(bbminx, bbminy, ax, ay, bx, by);

    //X方向的增量
    double a1 = (by - cy)*.5;
    double a2 = (cy - ay)*.5;
    double a3 = (ay - by)*.5;

    //Y方向的增量
    double b1 = (cx - bx)*.5;
    double b2 = (ax - cx)*.5;
    double b3 = (bx - ax)*.5;
    

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
            framebuffer.set(x, y, color);
        }
    }
}

void triangle_6_interpolation(int ax, int ay, int az, int bx, int by, int bz, int cx, int cy, int cz, TGAImage &framebuffer) {
    int bbminx = std::min(std::min(ax, bx), cx); // 三角形的包围盒
    int bbminy = std::min(std::min(ay, by), cy); // 由左上角和右下角定义
    int bbmaxx = std::max(std::max(ax, bx), cx);
    int bbmaxy = std::max(std::max(ay, by), cy);
    double total_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
    if (total_area<1) return; // 背面剔除 + 丢弃覆盖面积不足一个像素的三角形

#pragma omp parallel for
    for (int x=bbminx; x<=bbmaxx; x++) {
        for (int y=bbminy; y<=bbmaxy; y++) {
            double alpha = signed_triangle_area(x, y, bx, by, cx, cy) / total_area;
            double beta  = signed_triangle_area(x, y, cx, cy, ax, ay) / total_area;
            double gamma = signed_triangle_area(x, y, ax, ay, bx, by) / total_area;
            if (alpha<0 || beta<0 || gamma<0) continue; // 重心坐标为负 => 该像素在三角形之外
            unsigned char z = static_cast<unsigned char>(alpha * az + beta * bz + gamma * cz);
            TGAColor color = {static_cast<int>(alpha * 255), static_cast<int>(beta * 255), static_cast<int>(gamma * 255)};
            framebuffer.set(x, y, color);
        }
    }
}

int main(int argc, char** argv){
    constexpr int width  = 1024;
    constexpr int height = 1024;

    TGAImage framebuffer(width, height, TGAImage::RGB);

    Model newModel;
    bool flag = newModel.load("obj/diablo3_pose.obj");
    if(flag){
        // 只看数量不够——如果某个字段读错位，数量仍然可能对，但坐标会整体错掉。
        Vec3 firstVect = newModel.vert(0);
        std::cout << "第一个顶点：" << firstVect.x << " " << firstVect.y <<  " " << firstVect.z << std::endl;
        for(int i=0;i<newModel.nfaces();i++){
            // 画家算法：i 是绘制顺序，faceIndex(i) 才是该面的原始下标
            const int faceIdx = newModel.faceIndex(i);
            const std::array<int,3> modelFace = newModel.face(faceIdx);// 第 faceIdx 个面的 3 个顶点下标
            std::vector<screenVert> face;
            face.reserve(3);

            // 遍历三角形的 3 个顶点，将它们转换为屏幕坐标。
            for(int j = 0;j<3;j++){
                Vec3 nowVec = newModel.vert(modelFace.at(j));// 模型空间中的 3D 顶点
                float x = (nowVec.x + 1) * (width  / 2.f);
                float y = (1 - nowVec.y) * (height / 2.f);
                float z = (nowVec.z+1)/2.f;
                screenVert newVec(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z));
                face.push_back(newVec);
            }
            TGAColor rnd;
            for (int c=0; c<3; c++) rnd[c] = std::rand()%255;
            triangle_5(face.at(0).x, face.at(0).y, face.at(1).x, face.at(1).y, face.at(2).x, face.at(2).y, framebuffer, rnd);
            //triangle_6_interpolation(face.at(0).x, face.at(0).y, face.at(1).x, face.at(1).y, face.at(2).x, face.at(2).y,face.at(0).z, face.at(1).z, face.at(2).z, red, green, blue, framebuffer);
        }
        framebuffer.write_tga_file("out/framebuffer.tga", false);
    }else{
        std::cerr << "模型加载失败" << std::endl;
    }

    return 0;
}
