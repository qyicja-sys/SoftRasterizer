#pragma once
#include "vec/Vec.h"

struct Vertex {
    Vec3 position;
    Vec3 normal;
};

void normalizeNormals(Vertex& vn);