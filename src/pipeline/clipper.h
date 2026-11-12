#pragma once
#include "vec/Vec.h"
#include "pipeline/perspective.h"
#include <vector>

using namespace std;

class clipper{
    private:
        int ousideCount = 0;
        vector<Vec4> polygon;
        vector<Vec4> output;
        
    public:
        clipper();
        ~clipper();

        void leftplane();
        void rightplane();
        void bottomplane();
        void topplane();
        void nearplane();
        void farplane();

        void pushOutput(double DS,double DE,Vec4 S,Vec4 E);
        void isClipped(vector<Vec4> polygon);
        vector<vector<Vec4>> triangulation() const;

        // double getClip(int index) const;
        int getOutsideCount() const;

};
