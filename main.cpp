#include <cmath>
#include "tgaimage.h"
#include "model.h"
#include "geometry.h"

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
    constexpr int width = 800;
    constexpr int height = 800;
    Model* model = nullptr;
    if (2 == argc) {    // 命令行构造
        model = new Model(argv[1]);
    }
    else {  // 代码构造
        model = new Model("obj/african_head/african_head.obj");
    }
    TGAImage wireFrameImage(width, height, TGAImage::RGB);
    for (int i = 0; i < model->nfaces(); i++) {
        std::vector<int> face = model->face(i);
        for (int j = 0; j < 3; j++) {
            Vec3f v0 = model->vert(face[j]);
            Vec3f v1 = model->vert(face[(j + 1) % 3]);
            // 转换为屏幕坐标
            int x0 = (v0.x + 1) * width / 2;
            int y0 = (v0.y + 1) * height / 2;
            int x1 = (v1.x + 1) * width / 2;
            int y1 = (v1.y + 1) * height / 2;

            line(x0, y0, x1, y1, wireFrameImage, white);
        }
    }
    wireFrameImage.write_tga_file("wireFrameImage.tga");

    return 0;
}

