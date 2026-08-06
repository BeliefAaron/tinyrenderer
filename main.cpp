#include <cmath>
#include "tgaimage.h"
#include "model.h"
#include "geometry.h"
#include <algorithm>

constexpr int width  = 512;
constexpr int height = 512;
constexpr double M_PI = 3.1415926;
const double emptyDepth = -std::numeric_limits<double>::infinity();

constexpr TGAColor white   = {255, 255, 255, 255}; // attention, BGRA order
constexpr TGAColor green   = {  0, 255,   0, 255};
constexpr TGAColor red     = {  0,   0, 255, 255};
constexpr TGAColor blue    = {255,   0,   0, 255};
constexpr TGAColor yellow  = {  0, 200, 255, 255};

// 正交投影
std::tuple<int, int, double> projectToScreen(const vec3& v) {
    return {
        ((v.x + 1.) * width / 2),
        ((v.y + 1.) * height / 2),
        ((v.z + 1.) * 255. / 2)
    };
}

// 透视投影
vec3 persepProj(const vec3& v) {
    constexpr double camera_z = 3.;
    return v / (1. - v.z / camera_z);
}

vec3 rotByY(const vec3& v) {
    constexpr double angle = M_PI / 6;
    const mat<3,3> Ry = {{{std::cos(angle), 0, std::sin(angle)},
                    {0, 1, 0},
                    {-std::sin(angle), 0, std::cos(angle)}}};
    return Ry * v; 
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

void triangle(int ax, int ay, double az, int bx, int by, double bz, int cx, int cy, double cz, std::vector<std::vector<double>>& zBuffer, TGAImage &framebuffer, TGAImage &zBufferImage, TGAColor color) {
    int bbminX = std::max(0, std::min(ax, std::min(bx, cx)));   // 屏蔽屏幕外像素
    int bbmaxX = std::min(width - 1, std::max(ax, std::max(bx, cx)));
    int bbminY = std::max(0, std::min(ay, std::min(by, cy)));
    int bbmaxY = std::min(height - 1, std::max(ay, std::max(by, cy)));

    double total_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
    if (total_area < 1) return; // backface culling + discarding triangles that cover less than a pixel

    for(int y = bbminY; y <= bbmaxY; y++) {
        for(int x = bbminX; x <= bbmaxX; x++) {
            double alpha = signed_triangle_area(x, y, bx, by, cx, cy) / total_area;
            double beta  = signed_triangle_area(ax, ay, x, y, cx, cy) / total_area;
            double gamma = signed_triangle_area(ax, ay, bx, by, x, y) / total_area;
            bool bInside = alpha >= 0 && beta >= 0 && gamma >= 0;
            if(!bInside) continue;
            
            double z = (alpha * az + beta * bz + gamma * cz);

            if(z <= zBuffer[x][y]) continue;
            zBuffer[x][y] = z;
            framebuffer.set(x, y, color);
        }
    }
}

void drawZBuffer(const std::vector<std::vector<double>>& zBuffer, TGAImage &zBufferImage) {
    double minDepth = std::numeric_limits<double>::infinity();
    double maxDepth = -std::numeric_limits<double>::infinity();

    // 只统计真正写入过的像素
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            double depth = zBuffer[x][y];

            if (!std::isfinite(depth))
                continue;

            minDepth = std::min(minDepth, depth);
            maxDepth = std::max(maxDepth, depth);
        }
    }
    // 没有写入有效像素
    if(!std::isfinite(minDepth)) return;

    double depthRange = maxDepth - minDepth;
    for(int x = 0; x < width; x++) {
        for(int y = 0; y < height; y++) {
            double depth = zBuffer[x][y];
            if(!std::isfinite(depth)) continue;
            
            double normalized = 1.0;
            if(depthRange > std::numeric_limits<double>::epsilon()) {
                normalized = (depth - minDepth) / depthRange;
            }
            normalized = std::clamp(normalized, 0.0, 1.0);

            std::uint8_t gray = static_cast<std::uint8_t>(normalized * 255);
            zBufferImage.set(x, y, {gray});
        }
    }
}

int main(int argc, char** argv) {    
    Model* model = nullptr;
    if (2==argc) {
        model = new Model(argv[1]);  //命令行控制方式构造model
    } else {
        model = new Model("obj/diablo3_pose/diablo3_pose.obj"); //代码方式构造model
    }
    TGAImage framebuffer(width, height, TGAImage::RGB);
    TGAImage zBufferImage(width, height, TGAImage::GRAYSCALE);

    std::vector<std::vector<double>> zBuffer(width, std::vector<double>(height, emptyDepth));

    // int ax = 120, ay =  40, az =  255;
    // int bx = 400, by = 290, bz = 128;
    // int cx = 230, cy = 490, cz = 13;
    // triangle(ax, ay, az, bx, by, bz, cx, cy, cz, framebuffer, zBuffer, white);
    for(int i = 0; i < model->nfaces(); i++) {
        auto [x0, y0, z0] = projectToScreen(persepProj(rotByY(model->vert(model->face(i)[0]))));
        auto [x1, y1, z1] = projectToScreen(persepProj(rotByY(model->vert(model->face(i)[1]))));
        auto [x2, y2, z2] = projectToScreen(persepProj(rotByY(model->vert(model->face(i)[2]))));
        TGAColor rnd;
        for (int c=0; c<3; c++) rnd[c] = std::rand()%255;
        triangle(x0, y0, z0, x1, y1, z1, x2, y2, z2, zBuffer, framebuffer, zBufferImage, rnd);
    }

    drawZBuffer(zBuffer, zBufferImage);
    zBufferImage.write_tga_file("zBuffer.tga");
    framebuffer.write_tga_file("framebuffer.tga");
    return 0;
}

