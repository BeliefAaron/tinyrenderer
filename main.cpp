#include <cmath>
#include "tgaimage.h"
#include "model.h"
#include "geometry.h"

constexpr int width  = 512;
constexpr int height = 512;

constexpr TGAColor white   = {255, 255, 255, 255}; // attention, BGRA order
constexpr TGAColor green   = {  0, 255,   0, 255};
constexpr TGAColor red     = {  0,   0, 255, 255};
constexpr TGAColor blue    = {255, 128,  64, 255};
constexpr TGAColor yellow  = {  0, 200, 255, 255};

std::pair<int, int> projectToScreen(const Vec3f& v) {
    return {
        static_cast<int>((v.x + 1) * width / 2),
        static_cast<int>((v.y + 1) * height / 2)
    };
}

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

void triangle_scanline(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    /*
    * old school-scanline algorithm
    */
    // 排序, 使得 ay < by < cy
    if(ay > by) { std::swap(ax, bx); std::swap(ay, by);}
    if(ay > cy) { std::swap(ax, cx); std::swap(ay, cy);}  
    if(by > cy) { std::swap(bx, cx); std::swap(by, cy);}

    int total_height = cy - ay;
    if(by > ay) {
        int segment_height = by - ay;
        for(int y = ay; y <= by; y++) {
            int x1 = ax + (bx - ax) * (y - ay) / segment_height;
            int x2 = ax + (cx - ax) * (y - ay) / total_height;
            if(x1 > x2) std::swap(x1, x2);
            line(x1, y, x2, y, framebuffer, color);
        }
    }
    if(cy > by) {
        int segment_height = cy - by;
        for(int y = by; y <= cy; y++) {
            int x1 = bx + (cx - bx) * (y - by) / segment_height;
            int x2 = ax + (cx - ax) * (y - ay) / total_height; 
            if(x1 > x2) std::swap(x1, x2);
            line(x1, y, x2, y, framebuffer, color);
        }
    }
}

// 计算有符号三角形面积, 逆时针为正
double signed_triangle_area(int ax, int ay, int bx, int by, int cx, int cy) {
    return .5 *((by - ay) * (bx + ax) + (cy - by) * (cx + bx) + (ay -cy) * (ax + cx));
    // 等价于 return 0.5 * ((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
}

void triangle(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &framebuffer, TGAColor color) {
    int bbminX = std::min(ax, std::min(bx, cx));
    int bbmaxX = std::max(ax, std::max(bx, cx));
    int bbminY = std::min(ay, std::min(by, cy));
    int bbmaxY = std::max(ay, std::max(by, cy));
    double total_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
    if (total_area < 1) return; // backface culling + discarding triangles that cover less than a pixel

    for(int y = bbminY; y <= bbmaxY; y++) {
        for(int x = bbminX; x <= bbmaxX; x++) {
            double alpha = signed_triangle_area(x, y, bx, by, cx, cy) / total_area;
            double beta  = signed_triangle_area(ax, ay, x, y, cx, cy) / total_area;
            double gamma = signed_triangle_area(ax, ay, bx, by, x, y) / total_area;
            if(alpha >= 0 && beta >= 0 && gamma >= 0) {
                framebuffer.set(x, y, color);
            }
        }
    }
}

int main(int argc, char** argv) {    
    Model* model = nullptr;
    if (2==argc) {
        model = new Model(argv[1]);  //命令行控制方式构造model
    } else {
        model = new Model("obj/african_head/african_head.obj"); //代码方式构造model
    }
    TGAImage framebuffer(width, height, TGAImage::RGB);
    // triangle(  7, 45, 35, 100, 45,  60, framebuffer, red);
    // triangle(120, 35, 90,   5, 45, 110, framebuffer, white);
    // triangle(115, 83, 80,  90, 85, 120, framebuffer, green);
    for(int i = 0; i < model->nfaces(); i++) {
        auto [x0, y0] = projectToScreen(model->vert(model->face(i)[0]));
        auto [x1, y1] = projectToScreen(model->vert(model->face(i)[1]));
        auto [x2, y2] = projectToScreen(model->vert(model->face(i)[2]));
        TGAColor rnd;
        for (int c=0; c<3; c++) rnd[c] = std::rand()%255;
        triangle(x0, y0, x1, y1, x2, y2, framebuffer, rnd);
    }

    framebuffer.write_tga_file("framebuffer.tga");
    return 0;
}

