#include "viewPort.h"
#include "matrix/matrix.h"

void viewPort::setViewPort(const int x,const int y){
    viewPortMatrix = mat<4,4,double>{{width/2.0,0,0,x+width/2.0}, {0,-height/2.0f,0,y+height/2.0f}, {0,0,1,0}, {0,0,0,1}};
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
}

mat<4,4,double> viewPort::getViewPortMatrix() const{
    return viewPortMatrix;
}

int viewPort::getWidth() const{
    return width;
}
int viewPort::getHeight() const{
    return height;
}
void viewPort::setWidth(int width){
    this->width = width;
}
void viewPort::setHeight(int height){
    this->height = height;
}
