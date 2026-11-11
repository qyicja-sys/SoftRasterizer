#include "perspective.h"
#include "vec/Vec.h"
#include "matrix/matrix.h"
#include "zbuffer/zbuffer.h"

void perspective::perspectiveTransform(ZBuffer& zBuffer){
    double f = cam->getFocalLength();
    float N = zBuffer.getNearPlane();
    float F = zBuffer.getFarPlane();
    perspectiveMatrix = mat<4,4,double>{{f,0,0,0}, {0,f,0,0}, {0,0,(N+F)/(F-N),-2*N*F/(F-N)}, {0,0,1,0}};
}