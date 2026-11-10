#pragma once
#include "matrix.h"
#include "vec.h"
#include "model.h"

class camera{
    private:
        //投影属性
        float focalLength;
        int width, height;

        //摄像机属性
        Vec3 position;
        Vec3 rotation;
        mat<3, 3, float> rotationMatrix;//旋转矩阵
        
    public:
        camera(float focalLength, int width, int height);
        ~camera();

        //获取方向
        Vec3 getDirection(int x, int y) const;
        Vec3 getPosition() const;
        Vec3 getRotation() const;
        
        //设置属性(位置、旋转、焦距、视口大小)
        void setPosition(Vec3 position);
        void setRotation(Vec3 rotation);
        void setFocalLength(float focalLength);
        void setViewport(int width, int height);
        
        //世界坐标到相机坐标
        Vec3 worldToCamera(Vec3 vertex);
        //摄像机坐标到屏幕坐标
        screenVert projectToScreen(Vec3 vertex);
        //世界坐标到屏幕坐标
        screenVert worldToScreen(Vec3 vertex);
};