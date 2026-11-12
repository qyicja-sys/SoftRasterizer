#pragma once
#include "matrix/matrix.h"

class viewPort{
    private:
        mat<4,4,double> viewPortMatrix;
        int width, height;
    public:
        viewPort() : viewPortMatrix() {}
        void setViewPort(const int x,const int y);
        mat<4,4,double> getViewPortMatrix() const;

        int getWidth() const;
        int getHeight() const;

        void setWidth(int width);
        void setHeight(int height);
};
