#include "modelView.h"
#include "vec/Vec.h"
#include "matrix/matrix.h"

void modelView::lookat(Vec3 position, const Vec3 center, const Vec3 up) {
            Vec3 n = normalize(position-center);
            Vec3 l = normalize(crossProduct(up,n));
            Vec3 m = normalize(crossProduct(n, l));   
            modelViewMatrix = mat<4,4>{{{l.x,l.y,l.z,0}, {m.x,m.y,m.z,0}, {n.x,n.y,n.z,0}, {0,0,0,1}}} *
                mat<4,4>{{{1,0,0,-center.x}, {0,1,0,-center.y}, {0,0,1,-center.z}, {0,0,0,1}}};
}

mat<4,4,double> modelView::getModelViewMatrix() const{
    return modelViewMatrix;
}
