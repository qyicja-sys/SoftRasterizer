#include "Texture.h"

bool Texture::load(const std::string& filename) {
    if (!image.read_tga_file(filename)) {
        return false;
    }

    width = image.width();
    height = image.height();
    return true;
}

TGAColor Texture::sample(const Vec2& uv) const {
    double u = 0, v = 0;
    switch(wrapMode){
        case WrapMode:: Clamp:
            u = std::clamp(uv.x, 0.0, 1.0);
            v = std::clamp(uv.y, 0.0, 1.0);
            break;
        case WrapMode:: Repeat:
            //后续实现
            break;
        case WrapMode:: Mirror:
            //后续实现
            break;
    }
    // OBJ 的 v=0 在纹理底部，而 TGAImage 读取后第 0 行是图像顶部，这里翻转 v 轴对上方向
    int x = static_cast<int>(std::round(u * (width - 1)));
    int y = static_cast<int>(std::round((1.0 - v) * (height - 1)));
    return image.get(x, y);
}

int Texture::getWidth() const {
    return width;
}
int Texture::getHeight() const {
    return height;
}
