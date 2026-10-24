#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// TGA 文件头（Header）所需要的全部字段
#pragma pack(push,1)//内存对齐
struct TGAHeader {
    std::uint8_t  idlength = 0;//图像 ID 字段长度。通常为 0，表示没有附加 ID 信息
    std::uint8_t  colormaptype = 0;//颜色表类型（1 字节）。0 代表无颜色表，1 代表有
    std::uint8_t  datatypecode = 0;//图像类型码（1 字节）。决定了图像是压缩还是非压缩、彩色还是灰度（例如 2 代表未压缩真彩色）
    std::uint16_t colormaporigin = 0;//颜色表首址（2 字节）。颜色表的起始索引，无颜色表时忽略
    std::uint16_t colormaplength = 0;//颜色表长度（2 字节）。颜色表总共有多少项，无颜色表时忽略
    std::uint8_t  colormapdepth = 0;//颜色表项位数（1 字节）。每个颜色表项的位深（如 24 或 32 位），无颜色表时忽略
    std::uint16_t x_origin = 0;//图像起始坐标（各 2 字节）。图像左下角在屏幕上的坐标，通常设为 0
    std::uint16_t y_origin = 0;
    std::uint16_t width = 0;//图像宽高（各 2 字节）。以像素为单位，决定了图像尺寸。
    std::uint16_t height = 0;
    std::uint8_t  bitsperpixel = 0;//像素深度（1 字节）。每个像素占用的位数，例如 24 位（RGB）或 32 位（RGBA）
    std::uint8_t  imagedescriptor = 0;//图像描述符（1 字节）。包含了 Alpha 通道位数以及图像原点位置（左下或左上）等信息
};
#pragma pack(pop)//结束

//定义一个像素的颜色
struct TGAColor {
    std::uint8_t bgra[4] = {0,0,0,0};
    std::uint8_t bytespp = 4;
    std::uint8_t& operator[](const int i) { return bgra[i]; }
    const std::uint8_t& operator[](const int i) const { return bgra[i]; }
};

//整张图片，负责读取、保存、修改像素，以及管理图片数据
struct TGAImage {
    enum Format { GRAYSCALE=1, RGB=3, RGBA=4 };//表示图像使用的颜色通道格式
    TGAImage() = default;
    TGAImage(const int w, const int h, const int bpp, TGAColor c = {});//创建指定尺寸的图像
    bool  read_tga_file(const std::string filename);//从磁盘读取图片
    bool write_tga_file(const std::string filename, const bool vflip=true, const bool rle=true) const;//把图片保存到磁盘
    void flip_horizontally();//水平翻转
    void flip_vertically();//垂直翻转
    TGAColor get(const int x, const int y) const;//获取指定位置的颜色
    void set(const int x, const int y, const TGAColor &c);//修改指定位置的颜色
    //获取图片尺寸
    int width()  const;
    int height() const;
private:
    bool   load_rle_data(std::ifstream &in);//读取 RLE 压缩像素数据
    bool unload_rle_data(std::ofstream &out) const;//写出 RLE 压缩像素数据
    int w = 0, h = 0;
    std::uint8_t bpp = 0;
    std::vector<std::uint8_t> data = {};
};