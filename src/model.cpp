#include <iostream>
#include "model.h"
#include <fstream>
#include <sstream>
#include <algorithm>

int Model::nverts() const{
    return vecList.size();
}

int Model::nfaces() const{
    return faceList.size();
}

const Vec3& Model::vert(int i) const{
    return vecList.at(i);
}

const std::array<int,3>& Model::face(int i) const{
    return faceList.at(i);
}

bool Model::load(const std::string& filename){
    //1.创建file对象并打开
    std::ifstream file(filename);
    vecList.clear();faceList.clear();//防止重复加载时数据叠加

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
            float x,y,z;
            if(iss >> x >> y >> z){
                //组成一个顶点
                Vec3 list = {x, y, z};
                //进入顶点组
                vecList.push_back(list);
            }else{
                return false;
            }
        }else{
            if(type == "f"){
                std::array<int, 3> faceIndices;
                std::string vertexStr;
                int i = 0;

                // f 的每个 token 形如 "6/1/6"，三段分别是：
                //   顶点下标 / 纹理坐标下标 / 法线下标。
                // 线框只需要顶点下标，所以把 '/' 换成空格后只读第一个数。
                // OBJ 的索引从 1 开始，存进数组前统一减 1。
                while (i < 3 && iss >> vertexStr) {
                    // 1. 把 '/' 替换为空格
                    std::replace(vertexStr.begin(), vertexStr.end(), '/', ' ');
                    // 2. 传入字符串流
                    std::istringstream vertexStream(vertexStr);
                    // 3. 只取第一段：顶点索引
                    int v = 0;
                    if (vertexStream >> v) {
                        faceIndices[i] = v - 1;
                        i++;
                    }
                }
                if (i == 3) {
                    // 三个下标必须都在 [0, nverts) 范围内；
                    // 越界说明文件或解析逻辑有问题，直接让加载失败。
                    bool flag = true;
                    for(int j=0;j<3;j++){
                        if(!(faceIndices[j]>=0 && faceIndices[j]<Model::nverts())){
                            flag = false;
                            break;
                        }
                    }
                    if(flag){
                        faceList.push_back(faceIndices);
                    }else{
                        return false;
                    }
                }else{
                    return false;
                }
            }
        }
    }

    // 4. 文件会在 ifstream 析构时自动关闭，这里的 close 只是显式说明。
    file.close();

    return true;
}
