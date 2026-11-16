#include "perspective.h"
#include "vec/Vec.h"
#include "matrix/matrix.h"
#include "zbuffer/zbuffer.h"

void perspective::perspectiveTransform(ZBuffer& zBuffer, int width, int height){
    double Fov = cam.getFov();
    double q = 1/tan(Fov/2.0);
    double a = width / height;
    N = zBuffer.getNearPlane();
    F = zBuffer.getFarPlane();

    perspectiveMatrix = mat<4,4,double>{{q/a,0,0,0}, {0,a,0,0}, {0,0,(N+F)/(F-N),-2*N*F/(F-N)}, {0,0,1,0}};
}

void perspective::setCamera(camera& c){
    cam = c;
}

mat<4,4,double> perspective::getPerspectiveMatrix() const{
    return perspectiveMatrix;
}

