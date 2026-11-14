#include "modelView.h"
#include "vec/Vec.h"
#include "matrix/matrix.h"

void modelView::lookat(const camera& cam) {
            Vec3 n = cam.getRotationMatrix() * normalize(cam.getPosition()-cam.getCenter());
            Vec3 l = cam.getRotationMatrix() * normalize(crossProduct(cam.getUp(),n));
            Vec3 m = cam.getRotationMatrix() * normalize(crossProduct(n, l));   
            modelViewMatrix = mat<4,4>{{{l.x,l.y,l.z,0}, {m.x,m.y,m.z,0}, {-n.x,-n.y,-n.z,0}, {0,0,0,1}}} *
                mat<4,4>{{{1,0,0,-cam.getPosition().x}, {0,1,0,-cam.getPosition().y}, {0,0,1,-cam.getPosition().z}, {0,0,0,1}}};
}

mat<4,4,double> modelView::getModelViewMatrix() const{
    return modelViewMatrix;
}
