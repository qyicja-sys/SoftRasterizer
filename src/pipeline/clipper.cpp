#include "clipper.h"
#include "matrix/matrix.h"
#include "vec/Vec.h"
#include <vector>

using namespace std;

clipper::clipper(){
}

clipper::~clipper(){
}

Vertex Tinterpolate(Vertex S, Vertex E ,float DS,float DE){
    float t = -DS/(DE-DS);
    Vec4 position = S.position+(E.position-S.position)*t;
    Vec3 normal = (1-t)*S.normal+t*E.normal;
    Vec3 worldPos = S.worldPos + (E.worldPos - S.worldPos) * t;
    Vertex result = {position, normal, worldPos};
    return result;
}

void clipper::pushOutput(double DS,double DE,Vertex S,Vertex E){
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
        Vertex S = polygon[i];
        Vertex E = polygon[(i + 1) % polygon.size()];
        double DS = S.position.x+S.position.w;
        double DE = E.position.x+E.position.w;
        pushOutput(DS,DE,S,E);
    }
    polygon = output;
    output.clear();
}

void clipper::rightplane(){
   for (int i = 0; i < polygon.size(); i++) {
        Vertex S = polygon[i];
        Vertex E = polygon[(i + 1) % polygon.size()];
        double DS = S.position.w-S.position.x;
        double DE = E.position.w-E.position.x;
        pushOutput(DS,DE,S,E);
    }
    polygon = output;
    output.clear();
}

void clipper::bottomplane(){
    for (int i = 0; i < polygon.size(); i++) {
        Vertex S = polygon[i];
        Vertex E = polygon[(i + 1) % polygon.size()];
        double DS = S.position.y+S.position.w;
        double DE = E.position.y+E.position.w;
        pushOutput(DS,DE,S,E);
    }
    polygon = output;
    output.clear();
}

void clipper::topplane(){
    for (int i = 0; i < polygon.size(); i++) {
        Vertex S = polygon[i];
        Vertex E = polygon[(i + 1) % polygon.size()];
        double DS = S.position.w-S.position.y;
        double DE = E.position.w-E.position.y;
        pushOutput(DS,DE,S,E);
    }
    polygon = output;
    output.clear();
}

void clipper::nearplane(){
    for (int i = 0; i < polygon.size(); i++) {
        Vertex S = polygon[i];
        Vertex E = polygon[(i + 1) % polygon.size()];
        double DS = S.position.z+S.position.w;
        double DE = E.position.z+E.position.w;
        pushOutput(DS,DE,S,E);
    }
    polygon = output;
    output.clear();
}

void clipper::farplane(){
    for (int i = 0; i < polygon.size(); i++) {
        Vertex S = polygon[i];
        Vertex E = polygon[(i + 1) % polygon.size()];
        double DS = S.position.w-S.position.z;
        double DE = E.position.w-E.position.z;
        pushOutput(DS,DE,S,E);
    }
    polygon = output;
    output.clear();
}

void clipper::isClipped(vector<Vertex> polygon){
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

vector<vector<Vertex>> clipper::triangulation() const{
    vector<vector<Vertex>> clippedTriangle;
    if (polygon.size() < 3)
        return clippedTriangle;
    // 三角形三角剖分
    for (int i = 1; i < polygon.size() - 1; i++){
        Vertex A = polygon[0];
        Vertex B = polygon[i];
        Vertex C = polygon[i + 1];
        vector<Vertex> triangle;
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
