#include "camera/camera.h"
#include "matrix/matrix.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

mat<3, 3, float> rotationMatrixX(float angle){
    double a = angle*M_PI/180.0;
    mat<3, 3, float> Rx{{1,0,0}, {0,std::cos(a),-std::sin(a)}, {0,std::sin(a),std::cos(a)}};
    return Rx;
}

mat<3, 3, float> rotationMatrixY(float angle){
    double a = angle*M_PI/180.0;
    mat<3, 3, float> Ry{{std::cos(a),0,-std::sin(a)}, {0,1,0}, {std::sin(a),0,std::cos(a)}};
    return Ry;
}

mat<3, 3, float> rotationMatrixZ(float angle){
    double a = angle*M_PI/180.0;
    mat<3, 3, float> Rz{{std::cos(a),-std::sin(a),0}, {std::sin(a),std::cos(a),0}, {0,0,1}};
    return Rz;
}

camera::camera(float focalLength, int width, int height)
    : focalLength(focalLength), width(width), height(height),
      position(0, 0, 0), rotation(0, 0, 0),
      center(0, 0, 0), up(0, 1, 0) {
}

camera::~camera(){
}

float camera::getFocalLength() const{
    return focalLength;
}

int camera::getWidth() const{
    return width;
}

int camera::getHeight() const{
    return height;
}

void camera::setPosition(Vec3 position){
    this->position = position;
}

void camera::setRotation(Vec3 rotation){
    this->rotation = rotation;

    mat<3, 3, float> Rx = rotationMatrixX(rotation.x);
    mat<3, 3, float> Ry = rotationMatrixY(rotation.y);
    mat<3, 3, float> Rz = rotationMatrixZ(rotation.z);

    this->rotationMatrix = Rx*Ry*Rz;
}

void camera::setFocalLength(float focalLength){
    this->focalLength = focalLength;
}

void camera::setViewport(int width, int height){
    this->width = width;
    this->height = height;
}

Vec3 camera::worldToCamera(Vec3 world){
    // 相机沿 -z 方向观察，而 projectToScreen 的透视除法要求“相机前方 z > 0”。
    // 若直接返回 world - position，模型在相机前方时 z 恒为负（如相机在 z=5，
    // 模型 z≈0，得到 -5），会被 projectToScreen 的 z<=0 分支全部退化成 (0,0,0)，
    // 于是所有三角形面积为零，整张图全黑。因此这里把 z 取反。
    Vec3 cameraSpace = world - position;
    cameraSpace.z = -cameraSpace.z;
    return cameraSpace;
}

screenVert camera::projectToScreen(Vec3 world){
    if(world.z <= 0)
        return screenVert(0, 0, 0, false);
    int width = this->width;
    int height = this->height;
    Vec3 screen(static_cast<float>(world.x * focalLength / world.z + width / 2.0),
                 static_cast<float>(height / 2.0 - world.y * focalLength / world.z),
                 world.z);
    return screenVert(static_cast<int>(std::round(screen.x)), 
                      static_cast<int>(std::round(screen.y)), 
                      screen.z, true); // 深度保留浮点，round 成整数会只剩几档深度
}

screenVert camera::worldToScreen(Vec3 world){
    return projectToScreen(worldToCamera(world));
}
