#pragma once

#include <string>
#include <unordered_map>

struct Image {
    float* pixels;
    int w, h;
    enum Format {
        R=1,
        RG,
        RGB,
        RGBA,
    } format;
    float min;
    float max;
    bool is_cached;

    Image(float* pixels, int w, int h, Format format);
    ~Image();

    void getPixelValueAt(int x, int y, float* values, int d) const;

    static Image* load(const std::string& filename, bool force_load=true);

    static std::unordered_map<std::string, Image*> cache;
    static void flushCache();
};

