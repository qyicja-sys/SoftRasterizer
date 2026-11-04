#pragma once
#include <vector>


class ZBuffer{
    int width;
    int height;
    std::vector<unsigned char> zBuffer;

    public:
        ZBuffer(int width, int height);
        ~ZBuffer();

        void clear();
        unsigned char get(int x, int y) const;
        bool testAndSet(int x, int y, unsigned char depth);
};