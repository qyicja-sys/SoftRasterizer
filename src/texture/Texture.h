#pragma once
#include <algorithm>
#include "vec/Vec.h"
#include "TGAImage.h"

enum class WrapMode {
    Clamp,
    Repeat,
    Mirror
};

class Texture {
private:
    TGAImage image;
    int width;
    int height;

    WrapMode wrapMode = WrapMode::Clamp;

public:
    // 加载纹理
    bool load(const std::string& filename);

    // 获取纹理颜色（最近邻采样）
    TGAColor sample(const Vec2& uv) const;

    int getWidth() const;
    int getHeight() const;
};