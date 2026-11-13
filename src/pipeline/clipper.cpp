#include "clipper.h"
#include "matrix/matrix.h"
#include "vec/Vec.h"
#include <vector>

using namespace std;

clipper::clipper(){
}

clipper::~clipper(){
}

Vec4 Tinterpolate(Vec4 S, Vec4 E ,float DS,float DE){
    float t = -DS/(DE-DS);
    Vec4 result = S+(E-S)*t;
    return result;
}

void clipper::pushOutput(double DS,double DE,Vec4 S,Vec4 E){
    bool S_inside = DS >= 0;
    bool E_inside = DE >= 0;
     if(S_inside && E_inside){
        output.push_back(E);
    }else if(S_inside && !E_inside){
        output.push_back(Tinterpolate(S,E,DS,DE));//交点
    }else if(!S_inside && E_inside){
        output.push_back(Tinterpolate(E,S,DE,DS));//交点
        output.push_back(E);
    }
}

void clipper::leftplane(){
    for (int i = 0; i < polygon.size(); i++) {
        Vec4 S = polygon[i];
        Vec4 E = polygon[(i + 1) % polygon.size()];
        double DS = S.x+S.w;
        double DE = E.x+E.w;
        pushOutput(DS,DE,S,E);
    }
    polygon = output;
    output.clear();
}

void clipper::rightplane(){
   for (int i = 0; i < polygon.size(); i++) {
        Vec4 S = polygon[i];
        Vec4 E = polygon[(i + 1) % polygon.size()];
        double DS = S.w-S.x;
        double DE = E.w-E.x;
        pushOutput(DS,DE,S,E);
    }
    polygon = output;
    output.clear();
}

void clipper::bottomplane(){
    for (int i = 0; i < polygon.size(); i++) {
        Vec4 S = polygon[i];
        Vec4 E = polygon[(i + 1) % polygon.size()];
        double DS = S.y+S.w;
        double DE = E.y+E.w;
        pushOutput(DS,DE,S,E);
    }
    polygon = output;
    output.clear();
}

void clipper::topplane(){
    for (int i = 0; i < polygon.size(); i++) {
        Vec4 S = polygon[i];
        Vec4 E = polygon[(i + 1) % polygon.size()];
        double DS = S.w-S.y;
        double DE = E.w-E.y;
        pushOutput(DS,DE,S,E);
    }
    polygon = output;
    output.clear();
}

void clipper::nearplane(){
    for (int i = 0; i < polygon.size(); i++) {
        Vec4 S = polygon[i];
        Vec4 E = polygon[(i + 1) % polygon.size()];
        double DS = S.z+S.w;
        double DE = E.z+E.w;
        pushOutput(DS,DE,S,E);
    }
    polygon = output;
    output.clear();
}

void clipper::farplane(){
    for (int i = 0; i < polygon.size(); i++) {
        Vec4 S = polygon[i];
        Vec4 E = polygon[(i + 1) % polygon.size()];
        double DS = S.w-S.z;
        double DE = E.w-E.z;
        pushOutput(DS,DE,S,E);
    }
    polygon = output;
    output.clear();
}

void clipper::isClipped(vector<Vec4> polygon){
    this->polygon = polygon;
    if(polygon.size() < 3){
        return;
    }
    leftplane();
    rightplane();
    bottomplane();
    topplane();
    nearplane();
    farplane();
}

vector<vector<Vec4>> clipper::triangulation() const{
    vector<vector<Vec4>> clippedTriangle;
    if (polygon.size() < 3)
        return clippedTriangle;
    // 三角形三角剖分
    for (int i = 1; i < polygon.size() - 1; i++){
        Vec4 A = polygon[0];
        Vec4 B = polygon[i];
        Vec4 C = polygon[i + 1];
        vector<Vec4> triangle;
        triangle.push_back(A);
        triangle.push_back(B);
        triangle.push_back(C);
        clippedTriangle.push_back(triangle);
        //rasterizer.triangle(A, B, C);
    }
    return clippedTriangle;
}



// double clipper::getClip(int index) const{
//     return clipped[index];
// }
