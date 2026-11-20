#pragma once
#include "vec/Vec.h"

struct Vertex {
    Vec4 position; // clip 空间坐标
    Vec3 normal; // 模型空间法线
    Vec3 worldPos; // 世界空间位置，光照用
};

void normalizeNormals(Vertex& vn);