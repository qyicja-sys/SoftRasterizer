#pragma once
#include "matrix/matrix.h"
#include "vec/Vec.h"
#include "camera/camera.h"

class modelView{
    private:
        mat<4,4,double> modelViewMatrix;

    public:
        modelView() : modelViewMatrix() {}
        void lookat(const camera& cam);
        mat<4,4,double> getModelViewMatrix() const;
};


