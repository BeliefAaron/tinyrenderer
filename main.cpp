#include <cmath>
#include "tgaimage.h"
#include "model.h"
#include "geometry.h"
#include <algorithm>

constexpr int width  = 800;
constexpr int height = 800;
constexpr double M_PI = 3.1415926;
const double emptyDepth = -std::numeric_limits<double>::infinity();

constexpr TGAColor white   = {255, 255, 255, 255}; // attention, BGRA order
constexpr TGAColor green   = {  0, 255,   0, 255};
constexpr TGAColor red     = {  0,   0, 255, 255};
constexpr TGAColor blue    = {255,   0,   0, 255};
constexpr TGAColor yellow  = {  0, 200, 255, 255};

mat<4, 4> ModelView, Viewport, Perspective;

// 映射到以x, y为左下角起点的窗口里
void viewport(const int x, const int y, const int w, const int h) {
    Viewport = {{{w / 2., 0, 0, x + w / 2.}, 
                {0, h / 2., 0, y + h / 2.}, 
                {0, 0, 1, 0}, 
                {0, 0, 0, 1}}};
}

// 透视变形
void perspective(const double f) {
    Perspective = {{{1, 0, 0, 0},
                    {0, 1, 0, 0},
                    {0, 0, 1, 0},
                    {0, 0, -1/f, f}}};
}

// 视图变换(将世界坐标投影到相机坐标系)
void lookat(const vec3& eye, const vec3& center, const vec3& up) {
    vec3 n = normalized(eye - center);
    vec3 l = normalized(cross(up, n));
    vec3 m = normalized(cross(n, l));
    ModelView = mat<4, 4>{{{l.x, l.y, l.z, 0}, {m.x, m.y, m.z, 0}, {n.x, n.y, n.z, 0}, {0, 0, 0, 1}}} *
                mat<4, 4>{{{1, 0, 0, -center.x}, {0, 1, 0, -center.y}, {0, 0, 1, -center.z}, {0, 0, 0, 1}}}; 
}

void rasterize(const vec4 clip[3], std::vector<double> &zbuffer, TGAImage &framebuffer, const TGAColor color) {
    vec4 ndc[3] = {clip[0] / clip[0].w, clip[1] / clip[1].w, clip[2] / clip[2].w};
    vec2 screen[3] = {(Viewport * ndc[0]).xy(), (Viewport * ndc[1]).xy(), (Viewport * ndc[2]).xy()};
    
    mat<3, 3> vertexScreen = {{{screen[0].x, screen[0].y, 1},
                               {screen[1].x, screen[1].y, 1},
                               {screen[2].x, screen[2].y, 1}}};
    if(vertexScreen.det() < 1) return;  // 过滤背面三角形和渲染面积小于1像素的三角形

    auto [bboxMinX, bboxMaxX] = std::minmax({screen[0].x, screen[1].x, screen[2].x});
    auto [bboxMinY, bboxMaxY] = std::minmax({screen[0].y, screen[1].y, screen[2].y});
    for(int x = std::max<int>(0, bboxMinX); x <= std::min<int>(width - 1, bboxMaxX); x++) {
        for(int y = std::max<int>(0, bboxMinY); y <= std::min<int>(height - 1, bboxMaxY); y++) {
            vec3 bc = vertexScreen.invert_transpose() * vec3{static_cast<double>(x), static_cast<double>(y), 1}; // barycentric coordinates
            if(bc.x < 0 || bc.y < 0 || bc.z < 0) continue;
            double z = bc * vec3{ndc[0].z, ndc[1].z, ndc[2].z};
            if(zbuffer[x + y * framebuffer.width()] < z) {
                zbuffer[x + y * framebuffer.width()] = z;
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
        model = new Model("obj/diablo3_pose/diablo3_pose.obj"); //代码方式构造model
    }

    constexpr vec3    eye{0,0,4}; // camera position
    constexpr vec3 center{0,0,0};  // camera direction
    constexpr vec3     up{0,1,0};  // camera up vector

    lookat(eye, center, up);                              // build the ModelView   matrix
    perspective(norm(eye-center));                        // build the Perspective matrix
    viewport(width/16, height/16, width*7/8, height*7/8); // build the Viewport    matrix

    TGAImage framebuffer(width, height, TGAImage::RGB);
    std::vector<double> zbuffer(width*height, -std::numeric_limits<double>::max());

    for (int i=0; i<model->nfaces(); i++) { // iterate through all triangles
        vec4 clip[3];
        for (int d : {0,1,2}) {            // assemble the primitive
            vec3 v = model->vert(i, d);
            clip[d] = Perspective * ModelView * vec4{v.x, v.y, v.z, 1.};
        }
        TGAColor rnd;
        for (int c=0; c<3; c++) rnd[c] = std::rand()%255;
        rasterize(clip, zbuffer, framebuffer, rnd); // rasterize the primitive
    }

    framebuffer.write_tga_file("framebuffer.tga");
    return 0;
}

