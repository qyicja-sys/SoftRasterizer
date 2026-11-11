#pragma once
#include "matrix/matrix.h"
#include "vec/Vec.h"

class modelView{
    private:
        mat<4,4,double> modelViewMatrix;

    public:
        modelView() : modelViewMatrix() {}
        void lookat(Vec3 position, const Vec3 center, const Vec3 up);
};


