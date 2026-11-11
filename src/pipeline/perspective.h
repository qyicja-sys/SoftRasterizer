#pragma once
#include "matrix/matrix.h"
#include "vec/Vec.h"
#include "model/model.h"
#include "camera/camera.h"

class perspective{
    private:
        mat<4,4,double> perspectiveMatrix;
        camera* cam;

    public:
        perspective() : perspectiveMatrix(), cam(nullptr) {}

        void setCamera(camera* c){
            cam = c;
        }

        void perspectiveTransform(ZBuffer& zBuffer);
};
