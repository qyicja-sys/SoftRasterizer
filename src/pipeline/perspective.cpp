#include "perspective.h"
#include "vec/Vec.h"
#include "matrix/matrix.h"
#include "zbuffer/zbuffer.h"

void perspective::perspectiveTransform(ZBuffer& zBuffer, int width, int height){
    double f = cam.getFocalLength();
    double sx = f / (width / 2.0 ); // 500 / 512 ≈ 0.977 double sy = f / (height / 2.0 );
    double sy = f / (height / 2.0 );
    N = zBuffer.getNearPlane();
    F = zBuffer.getFarPlane();

    perspectiveMatrix = mat<4,4,double>{{sx,0,0,0}, {0,sy,0,0}, {0,0,(N+F)/(F-N),-2*N*F/(F-N)}, {0,0,1,0}};
}

void perspective::setCamera(camera& c){
    cam = c;
}

mat<4,4,double> perspective::getPerspectiveMatrix() const{
    return perspectiveMatrix;
}

