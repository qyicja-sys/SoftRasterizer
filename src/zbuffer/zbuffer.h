#pragma once
#include <vector>


class ZBuffer{
    int width;
    int height;
    float nearPlane;
    float farPlane;
    std::vector<unsigned char> zBuffer;

    public:
        ZBuffer() : width(0), height(0), nearPlane(0.0), farPlane(0.0), zBuffer() {}
        ZBuffer(int width, int height);
        ~ZBuffer();

        void setNearPlane(double nearPlane);
        void setFarPlane(double farPlane);

        float getNearPlane() const;
        float getFarPlane() const;

        void clear();
        unsigned char get(int x, int y) const;
        bool testAndSet(int x, int y, unsigned char depth);
};