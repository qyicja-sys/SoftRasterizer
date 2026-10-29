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

//计算三角形有向面积
double signed_triangle_area(int ax, int ay, int bx, int by, int cx, int cy) {
    return .5*((by-ay)*(bx+ax) + (cy-by)*(cx+bx) + (ay-cy)*(ax+cx));
}

//三个点的叉积计算
int cross_product(int ax, int ay, int bx, int by, int cx, int cy) {
    return (bx-ax)*(cy-ay) - (by-ay)*(cx-ax);
}

//绘制三角形
//扫描线填充(我自己想的直线方程反函数填充，可以优化)
void triangle_1(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    line(ax, ay, bx, by, framebuffer, color);
    line(bx, by, cx, cy, framebuffer, color);
    line(cx, cy, ax, ay, framebuffer, color);

    // 1. 算出三条边的斜率
    float k_AB = static_cast<float>(by - ay) / (bx - ax);
    float b_AB = ay - k_AB * ax;

    float k_BC = static_cast<float>(cy - by) / (cx - bx);
    float b_BC = by - k_BC * bx;

    float k_AC = static_cast<float>(cy - ay) / (cx - ax);
    float b_AC = ay - k_AC * ax;

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
                float t = static_cast<float>(y - y1) / (y2 - y1);
                float x = x1 + t * (x2 - x1);
                intersect_x.push_back(x);
            }
        };

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
    line(ax, ay, bx, by, framebuffer, color);
    line(bx, by, cx, cy, framebuffer, color);
    line(cx, cy, ax, ay, framebuffer, color);

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

//绘制三角形(扫描线填充，边函数+增量计算)
void triangle_3(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    // line(ax, ay, bx, by, framebuffer, color);
    // line(bx, by, cx, cy, framebuffer, color);
    // line(cx, cy, ax, ay, framebuffer, color);

    int bbminx = std::min(std::min(ax, bx), cx); // 三角形的包围盒
    int bbminy = std::min(std::min(ay, by), cy); // 由左上角和右下角定义
    int bbmaxx = std::max(std::max(ax, bx), cx);
    int bbmaxy = std::max(std::max(ay, by), cy);
    // 并行填充包围盒内的像素，并利用线程池加速计算
    // for (int x=bbminx; x<=bbmaxx; x++) {
    //     for (int y=bbminy; y<=bbmaxy; y++) {
    //         framebuffer.set(x, y, color);
    //     }
    // }

    // 按 y 坐标升序排列顶点 a、b、c（冒泡排序，好耶！）
    if (ay>by) { std::swap(ax, bx); std::swap(ay, by); }
    if (ay>cy) { std::swap(ax, cx); std::swap(ay, cy); }
    if (by>cy) { std::swap(bx, cx); std::swap(by, cy); }

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

int main(int argc, char** argv){
    // 画布（帧缓冲）尺寸：800×800 像素。
    // 下面的坐标映射会用到 width/2 和 height/2，改分辨率只需要改这两个常量。
    constexpr int width  = 1024;
    constexpr int height = 1024;

    // 创建帧缓冲。TGAImage 构造函数会用默认颜色 TGAColor{}（全 0 = 黑色）
    // 填满整张图，所以不需要手动刷黑背景，画上去的线会覆盖对应像素。
    TGAImage framebuffer(width, height, TGAImage::RGB);

    // 加载 OBJ 模型。路径是相对“当前工作目录”的：
    // VSCode 的 launch.json 把 cwd 设为项目根目录，所以能找到 obj/；
    // 如果直接在 build/bin 目录下启动 exe，会因找不到文件而加载失败。
    Model newModel;
    bool flag = newModel.load("obj/african_head.obj");
    if(flag){
        // 只看数量不够——如果某个字段读错位，数量仍然可能对，但坐标会整体错掉。
        Vec3 firstVect = newModel.vert(0);
        std::cout << "第一个顶点：" << firstVect.x << " " << firstVect.y <<  " " << firstVect.z << std::endl;

        // 遍历模型里的每个三角形面（本模型共 5022 个）。
        // face(i) 返回的是 3 个“顶点下标”，不是坐标；
        // 必须再用 vert(下标) 查表，才能拿到真正的 3D 坐标。
        for(int i=0;i<newModel.nfaces();i++){
            const std::array<int,3> modelFace = newModel.face(i);// 第 i 个面的 3 个顶点下标

            // 先收集这个三角形的 3 个屏幕点，再统一画线。
            // 这里创建空 vector 后 push 三次；不能写成 vector(3) 再 push，
            // 那样会先得到 3 个默认元素，再加 3 个新元素，共 6 个。
            std::vector<screenVert> face;
            face.reserve(3);

            // 遍历三角形的 3 个顶点，将它们转换为屏幕坐标。
            for(int j = 0;j<3;j++){
                Vec3 nowVec = newModel.vert(modelFace.at(j));// 模型空间中的 3D 顶点

                // ================= 坐标映射：模型空间 → 屏幕空间 =================
                // 两套坐标系不一样：
                //   模型坐标：原点在模型中心，x 向右，y 向上，范围约 [-1, 1]；
                //   屏幕坐标：原点在画面左上角，x 向右，y 向下，范围 [0, 800)。
                //
                // x 方向（不需要翻转）：
                //   模型 x=-1（最左）→ 屏幕 x=0
                //   模型 x=+1（最右）→ 屏幕 x=800
                //   公式：屏幕x = (x + 1) / 2 * 宽 = (x + 1) * (width / 2)
                //   先把 [-1,1] 平移成 [0,2]，再乘半宽缩放到 [0,800]。
                //
                // y 方向（必须翻转）：
                //   模型 y 越大表示越高；屏幕 y 越大表示越靠下，两者方向相反。
                //   如果照搬 x 的写法写成 (y + 1) * 半高：
                //       模型 y=+1（头顶）→ 屏幕 y=800（最底部）
                //       模型 y=-1（脚底）→ 屏幕 y=0  （最顶部）
                //   结果就是头朝下、脚朝上。
                //   所以先取反：屏幕y = (1 - y) / 2 * 高 = (1 - y) * (height / 2)
                //       模型 y=+1（头顶）→ (1-1)=0 → 屏幕 y=0  （最上方）
                //       模型 y= 0（腰部）→ (1-0)=1 → 屏幕 y=400（正中间）
                //       模型 y=-1（脚底）→ (1+1)=2 → 屏幕 y=800（最下方）
                //   等价于把 [-1,1] 映射成 [2,0]（递减），方向就反过来了。
                //
                // z 方向（本轮不参与）：
                //   屏幕是二维的，line() 只需要 x、y；z 原样保存在 screenVert 中，
                //   等做 z-buffer（判断哪个三角形在前、谁被遮挡）时才会用到。
                //
                // 最后转 int：像素坐标必须是整数，float→int 会截断小数。
                // 线框用截断足够；以后做三角形填充时再讨论更合适的取整方式。
                // =============================================================
                float x = (nowVec.x + 1) * (width  / 2.f);
                float y = (1 - nowVec.y) * (height / 2.f);
                screenVert newVec(static_cast<int>(x), static_cast<int>(y), nowVec.z);
                face.push_back(newVec);
            }

            // 画三角形的三条边：0-1、0-2、1-2，首尾相连正好闭合。
            TGAColor rnd;
            for (int c=0; c<3; c++) rnd[c] = std::rand()%255;
                triangle_4(face.at(0).x, face.at(0).y, face.at(1).x, face.at(1).y, face.at(2).x, face.at(2).y, framebuffer, rnd);
        }

        // 输出 TGA。第二个参数 vflip 必须显式传 false：
        // write_tga_file 默认 vflip=true，会把文件头声明成“左下角为原点”，
        // 看图软件于是把内存第 0 行画在画面底部，相当于又上下翻了一次。
        // 上面的映射已经把“内存第 0 行 = 画面顶部”对齐好了，
        // 所以这里声明“左上角为原点”（vflip=false），避免翻转两次。
        framebuffer.write_tga_file("out/framebuffer.tga", false);
    }else{
        std::cerr << "模型加载失败" << std::endl;
    }

    return 0;
}
