#pragma once
#include "vec/Vec.h"
#include "vertex/vertex.h"
#include "pipeline/perspective.h"
#include <vector>

using namespace std;

class clipper{
    private:
        int ousideCount = 0;
        vector<Vertex> polygon;
        vector<Vertex> output;
        
    public:
        clipper();
        ~clipper();

        void leftplane();
        void rightplane();
        void bottomplane();
        void topplane();
        void nearplane();
        void farplane();

        void pushOutput(double DS,double DE,Vertex S,Vertex E);
        void isClipped(vector<Vertex> polygon);
        vector<vector<Vertex>> triangulation() const;

        // double getClip(int index) const;
        int getOutsideCount() const;

};
