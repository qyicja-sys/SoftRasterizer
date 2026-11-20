#include <iostream>
#include "model/model.h"
#include <fstream>
#include <sstream>
#include <algorithm>

int Model::nverts_V() const{
    return vecList.size();
}

int Model::nfaces_V() const{
    return faceVertList.size();
}

int Model::nfaces_T() const{
    return faceTexList.size();
}

int Model::nverts_N() const{
    return vertexList.size();
}

int Model::nfaces_N() const{
    return facenormalList.size();
}

const Vec3& Model::vertV(int i) const{
    return vecList.at(i);
}

const std::array<int,3>& Model::faceVert(int i) const{
    return faceVertList.at(i);
}

const Vec3& Model::vertN(int i) const{
    return vertexList.at(i).normal;
}

const std::array<int,3>& Model::faceNormal(int i) const{
    return facenormalList.at(i);
}



bool Model::load(const std::string& filename){
    //1.创建file对象并打开
    std::ifstream file(filename);
    vecList.clear();faceVertList.clear();//防止重复加载时数据叠加
    vertexList.clear();facenormalList.clear();

    //2.检查是否打开
    if (!file.is_open()) {
        std::cerr << "无法打开文件: " << filename << std::endl;
        return false;
    }

    //3.读取数据
    // 逐行解析。只关心两类行：
    //   v  x y z                     -> 顶点坐标
    //   f  v/vt/vn v/vt/vn v/vt/vn   -> 三角形（三个顶点下标，从 1 开始）
    // vt（纹理坐标）和 vn（法线）这一轮用不到，直接忽略。
    std::string line;
    while (std::getline(file, line)) {
        //把行数据传入数据流
        std::istringstream iss(line);

        //判断类型：必须比较完整的单词。
        //不能只看首字母——v、vt、vn 都以 'v' 开头。
        std::string type;
        iss >> type;
        if(type == "v"){
            // v 行后面是三个浮点数；读失败说明这一行格式不对
            double x,y,z;
            if(iss >> x >> y >> z){
                //组成一个顶点
                Vec3 list = {x, y, z};
                //进入顶点组
                vecList.push_back(list);
            }else{
                return false;
            }
        }
        //加载顶点法线和uv
        if(type == "vn"){
            double x,y,z;
            if(iss >> x >> y >> z){
                //组成一个法线
                Vec3 list = {x, y, z};
                //进入顶点组（位置留空，法线存进 normal）
                vertexList.push_back({Vec4(0,0,0,1), list});
            }else{
                return false;
            }
        }

        //加载三角形(面)
        if(type == "f"){
            std::array<int, 3> faceIndices;
            std::array<int, 3> normalIndices;
            std::string vertexStr;
            int i = 0;

            // f 的每个 token 形如 "6/1/6"，三段分别是：
            //   顶点下标 / 纹理坐标下标 / 法线下标。
            // 这里取第一段（顶点）和第三段（法线），纹理下标跳过。
            // OBJ 的索引从 1 开始，存进数组前统一减 1。
            while (i < 3 && iss >> vertexStr) {
                // 1. 把 '/' 替换为空格
                std::replace(vertexStr.begin(), vertexStr.end(), '/', ' ');
                // 2. 传入字符串流
                std::istringstream vertexStream(vertexStr);
                // 3. 依次取：顶点下标 / 纹理下标 / 法线下标
                int v = 0, vt = 0, vn = 0;
                if (vertexStream >> v) {
                    faceIndices[i] = v - 1;
                    // 有第三段就说明这一项带法线；没有则记 -1
                    if (vertexStream >> vt >> vn) {
                        normalIndices[i] = vn - 1;
                    } else {
                        normalIndices[i] = -1;
                    }
                    i++;
                }
            }
            if (i == 3) {
                // 三个下标必须都在 [0, nverts) 范围内；
                // 越界说明文件或解析逻辑有问题，直接让加载失败。
                bool flag = true;
                for(int j=0;j<3;j++){
                    if(!(faceIndices[j]>=0 && faceIndices[j]<Model::nverts_V())){
                        flag = false;
                        break;
                    }
                    if(normalIndices[j]>=0 && normalIndices[j]>=Model::nverts_N()){
                        flag = false;
                        break;
                    }
                }
                if(flag){
                    faceVertList.push_back(faceIndices);
                    facenormalList.push_back(normalIndices);
                }else{
                    return false;
                }
            }else{
                return false;
            }
        }
    }

    // 4. 文件会在 ifstream 析构时自动关闭，这里的 close 只是显式说明。
    file.close();

    return true;
}

mat<4,4,double> Model::getModelMatrix() const{
    return modelMatrix;
}

void Model::setModelMatrix(Vec3 position, Vec3 rotation, Vec3 scale){
    modelMatrix = matrix::translate(position.x, position.y, position.z)
        * matrix::rotationX(rotation.x)
        * matrix::rotationY(rotation.y)
        * matrix::rotationZ(rotation.z)
        * matrix::scale(scale.x, scale.y, scale.z);
}

void Model::setMaterial(Vec3 Ka, Vec3 Kd, Vec3 Ks, double Ns){
    material = {Ka, Kd, Ks, Ns};
    this->material = material;
}

void Model::setLight(Vec3 position, Vec3 ambient, Vec3 diffuse, Vec3 specular){
    light = {position, ambient, diffuse, specular};
    this->light = light;
}

Material Model::getMaterial() const{
    return material;
}

Light Model::getLight() const{
    return light;
}
