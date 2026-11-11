#include "viewPort.h"
#include "matrix/matrix.h"

void viewPort::setViewPort(const int x,const int y){
    viewPortMatrix = mat<4,4,double>{{width/2.0,0,0,x+width/2.0}, {0,height/2.0f,0,y+height/2.0f}, {0,0,1,0}, {0,0,0,1}};
}

int viewPort::getWidth() const{
    return width;
}
int viewPort::getHeight() const{
    return height;
}
void viewPort::setWidth(int width){
    this->width = width;
}
void viewPort::setHeight(int height){
    this->height = height;
}
