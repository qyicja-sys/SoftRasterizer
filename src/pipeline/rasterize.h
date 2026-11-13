#pragma once
#include <vector>
#include "tgaimage.h"
#include "matrix/matrix.h"
#include "viewPort.h"
#include "perspective.h"
#include "clipper.h"
#include "modelView.h"
#include "zbuffer/zbuffer.h"
#include "model/model.h"
#include "vec/Vec.h"

class rasterize{
    private:
        modelView TGAmodelView;
        perspective TGAperspective;
        clipper TGAclipper;
        viewPort TGAviewPort;
        ZBuffer zBuffer;
        Model m;
    public:
        rasterize() : TGAmodelView(), TGAperspective(), TGAclipper(), TGAviewPort(), zBuffer(), m() {}

        Vec4 rasterizeGetClipVec(Vec4& vec);

        void rasterizeInit(camera& camera, int width, int height);
        void rasterizeSet(modelView modelView, perspective perspective, clipper clipper, viewPort viewPort, ZBuffer zBuffer);
        void rasterize_colorMap(const vector<Vec4>& clip, TGAImage& framebuffer, const TGAColor color);
        void rasterize_depthMap();
        void rasterize_updateZBuffer(ZBuffer& zBuffer);
        void rasterize_updatePerspective();

        
};
