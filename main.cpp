#include <cmath>
#include "tgaimage.h"

constexpr TGAColor white   = {255, 255, 255, 255}; // attention, BGRA order
constexpr TGAColor green   = {  0, 255,   0, 255};
constexpr TGAColor red     = {  0,   0, 255, 255};
constexpr TGAColor blue    = {255, 128,  64, 255};
constexpr TGAColor yellow  = {  0, 200, 255, 255};

void line(int x0, int y0, int x1, int y1, TGAImage &framebuffer, TGAColor color) 
{
    bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
    if (steep) {
        std::swap(x0, y0);
        std::swap(x1, y1);
    }
    if (x1 < x0) {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    int y = y0;
    int ierror = 0;
    for (int x = x0; x <= x1; x++) {
        if (steep) {
            framebuffer.set(y, x, color);
        }
        else {
            framebuffer.set(x, y, color);
        }
        ierror += 2 * std::abs(y1 - y0);
        /*if (ierror > x1 - x0) {
            y += (y1 > y0) ? 1 : -1;
            ierror -= 2 * (x1 - x0);
        }*/
        // 上述if判断会增加绘制时间，我们可以选择优化成以下branchless形式: 
        y += ((y1 > y0) ? 1 : -1) * (ierror > x1 - x0);
        ierror -= 2 * (x1 - x0) * (ierror > x1 - x0);
    }
}

int main(int argc, char** argv) {
    constexpr int width  = 64;
    constexpr int height = 64;
    TGAImage framebuffer(width, height, TGAImage::RGB);

    int ax =  7, ay =  3;
    int bx = 12, by = 37;
    int cx = 62, cy = 53;

    line(ax, ay, bx, by, framebuffer, blue);
    line(cx, cy, bx, by, framebuffer, green);
    line(cx, cy, ax, ay, framebuffer, yellow);
    line(ax, ay, cx, cy, framebuffer, red);

    framebuffer.set(ax, ay, white);
    framebuffer.set(bx, by, white);
    framebuffer.set(cx, cy, white);

    framebuffer.write_tga_file("framebuffer.tga");
    return 0;
}

