#pragma once
#include "matrix/matrix.h"
#include "vec/Vec.h"
#include "model/model.h"
#include "camera/camera.h"
#include "zbuffer/zbuffer.h"

class perspective{
    private:
        mat<4,4,double> perspectiveMatrix;
        float N ;
        float F ;
        camera cam;

    public:
        perspective() : perspectiveMatrix(), N(), F(), cam() {}

        void setCamera(camera& c);

        void perspectiveTransform(ZBuffer& zBuffer, int width, int height);

        mat<4,4,double> getPerspectiveMatrix() const;
};
