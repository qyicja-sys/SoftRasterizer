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
#include "camera/camera.h"
#include "vertex/vertex.h"



struct IShader {
    virtual std::pair<bool,TGAColor> fragment(const Vec3 bar) const = 0;
    virtual Vec3 calculateLight(camera& camera, Vec3 normalVec, Vec3 fragmentVec) = 0;
};

class rasterize{
    private:
        camera TGAcamera;
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
        void rasterizeSet(camera& camera, modelView modelView, perspective perspective, clipper clipper, viewPort viewPort, ZBuffer zBuffer, Model& model);
        void rasterizeSetCamera(camera& camera);
        void rasterizeSetZbuffer(ZBuffer zBuffer);
        void rasterizeSetModel(Model& model);
        void rasterizeSetModelView(camera& camera, Model& model);

        modelView getModelView() const;


        //单独初始化modelView
        void initModelView(camera& camera, Model& model);
        //获取modelView矩阵
        mat<4,4,double> getModelViewMatrix() const;

        void rasterize_colorMap(const vector<Vertex>& clip, IShader& shader, TGAImage& framebuffer);
        void rasterize_depthMap(const vector<Vertex>& clip, const IShader& shader, TGAImage& zMap);
        void rasterize_updateZBuffer(ZBuffer& zBuffer);
        void rasterize_updatePerspective();

        
};
