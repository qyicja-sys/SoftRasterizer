#include "tgaimage.h"
#include "model.h"
#include <iostream>
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

int main(int argc, char** argv){
    // 画布（帧缓冲）尺寸：800×800 像素。
    // 下面的坐标映射会用到 width/2 和 height/2，改分辨率只需要改这两个常量。
    constexpr int width  = 800;
    constexpr int height = 800;

    // 创建帧缓冲。TGAImage 构造函数会用默认颜色 TGAColor{}（全 0 = 黑色）
    // 填满整张图，所以不需要手动刷黑背景，画上去的线会覆盖对应像素。
    TGAImage framebuffer(width, height, TGAImage::RGB);

    // 加载 OBJ 模型。路径是相对“当前工作目录”的：
    // VSCode 的 launch.json 把 cwd 设为项目根目录，所以能找到 obj/；
    // 如果直接在 build/bin 目录下启动 exe，会因找不到文件而加载失败。
    Model newModel;
    bool flag = newModel.load("obj/diablo3_pose.obj");
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
            // 相邻面共用的边会被重复绘制多次，线框阶段无所谓，不必去重。
            line(face.at(0).x, face.at(0).y, face.at(1).x, face.at(1).y, framebuffer, red);
            line(face.at(0).x, face.at(0).y, face.at(2).x, face.at(2).y, framebuffer, red);
            line(face.at(1).x, face.at(1).y, face.at(2).x, face.at(2).y, framebuffer, red);
        }

        // 输出 TGA。第二个参数 vflip 必须显式传 false：
        // write_tga_file 默认 vflip=true，会把文件头声明成“左下角为原点”，
        // 看图软件于是把内存第 0 行画在画面底部，相当于又上下翻了一次。
        // 上面的映射已经把“内存第 0 行 = 画面顶部”对齐好了，
        // 所以这里声明“左上角为原点”（vflip=false），避免翻转两次。
        framebuffer.write_tga_file("framebuffer.tga", false);
    }else{
        std::cerr << "模型加载失败" << std::endl;
    }

    return 0;
}
