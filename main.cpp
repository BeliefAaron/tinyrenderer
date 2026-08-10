#include <cstdlib>
#include "our_gl.h"
#include "model.h"
#include <string>

extern mat<4,4> ModelView, Perspective; // "OpenGL" state matrices and
extern std::vector<double> zbuffer;     // the depth buffer

struct PhoneShader : IShader {
    const Model &model;
    vec3 l;       // light direction in eye coordinates
    vec3 tri[3];  // triangle in eye coordinates
    vec3 eye;     // camera position in eye coordinates

    PhoneShader(const vec3 &light, const Model &m, const vec3 &e) : model(m) {
        l = normalized((ModelView*vec4{light.x, light.y, light.z, 0.}).xyz());  // transform the light vector to view coordinates
        eye = (ModelView * vec4{e.x, e.y, e.z, 1.}).xyz();
    }

    virtual vec4 vertex(const int face, const int vert) {
        vec3 v = model.vert(face, vert);                          // current vertex in object coordinates
        vec4 gl_Position = ModelView * vec4{v.x, v.y, v.z, 1.};
        tri[vert] = gl_Position.xyz();                            // in eye coordinates
        return Perspective * gl_Position;                         // in clip coordinates
    }

    virtual std::pair<bool,TGAColor> fragment(const vec3 bar) const {
        TGAColor fragColor = {255, 255, 255, 255};
        vec3 n = normalized(cross(tri[1] - tri[0], tri[2] - tri[0])); // 法线向量
        vec3 r = normalized(2*n*(n*l) - l);   // 反射光向量
        vec3 fragPos = tri[0] * bar.x + tri[1] * bar.y + tri[2] * bar.z;

        vec3 viewDir = normalized(eye - fragPos);

        double ambient = 0.3; // 环境光
        double diffuse = std::max(0., n*l); // 漫反射
        double specular = std::pow(std::max(0., r * viewDir), 32); // 镜面反射强度，因为相机在相机坐标系中的z轴正方向，所以取r.z作为视线方向与反射光的夹角余弦值

        for(int i = 0; i < 3; i++) {
            fragColor[i] *= std::min(1., ambient + diffuse + 5*specular);
        }
        return {false, fragColor};                                    // do not discard the pixel
    }
};

int main(int argc, char** argv) {    
    std::string filename = argc == 2 ? argv[1] : "obj/diablo3_pose/diablo3_pose.obj";

    constexpr int width  = 800;      // output image size
    constexpr int height = 800;
    constexpr vec3 light{ 1, 1, 1}; // light source 
    constexpr vec3    eye{ 0, 0, 2}; // camera position
    constexpr vec3 center{ 0, 0, 0}; // camera direction
    constexpr vec3     up{ 0, 1, 0}; // camera up vector

    lookat(eye, center, up);                                   // build the ModelView   matrix
    init_perspective(norm(eye-center));                        // build the Perspective matrix
    init_viewport(width/16, height/16, width*7/8, height*7/8); // build the Viewport    matrix
    init_zbuffer(width, height);
    TGAImage framebuffer(width, height, TGAImage::RGB);

    Model model(filename);
    PhoneShader shader(light, model, eye);
    for (int f=0; f<model.nfaces(); f++) {      // iterate through all facets
        Triangle clip = { shader.vertex(f, 0),  // assemble the primitive
                            shader.vertex(f, 1),
                            shader.vertex(f, 2) };
        rasterize(clip, shader, framebuffer);   // rasterize the primitive
    }

    framebuffer.write_tga_file("framebuffer.tga");
    return 0;
}

