#pragma once
#include "matrix/matrix.h"
#include "vec/Vec.h"
#include "model/model.h"
#include "camera/camera.h"
#include "zbuffer/zbuffer.h"

class perspective{
    private:
        mat<4,4,double> perspectiveMatrix;
        camera cam;

    public:
        perspective() : perspectiveMatrix(), cam() {}

        void setCamera(camera& c);

        void perspectiveTransform(ZBuffer& zBuffer);

        mat<4,4,double> getPerspectiveMatrix() const;
};
