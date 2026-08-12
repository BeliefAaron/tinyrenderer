#include <cstdlib>
#include "our_gl.h"
#include "model.h"
#include <string>

extern mat<4,4> ModelView, Perspective; // "OpenGL" state matrices and
extern std::vector<double> zbuffer;     // the depth buffer

struct PhoneShader : IShader {
    const Model &model;
    vec4 l;       // light direction in eye coordinates
    vec4 tri[3];  // triangle in eye coordinates
    vec4 eye;     // camera position in eye coordinates
    vec2 varying_uv[3];
    vec4 varying_norm[3];

    PhoneShader(const vec3 &light, const Model &m, const vec3 &e) : model(m) {
        l = normalized((ModelView*vec4{light.x, light.y, light.z, 0.}));  // transform the light vector to view coordinates
        eye = (ModelView * vec4{e.x, e.y, e.z, 1.});
    }

    virtual vec4 vertex(const int face, const int vert) {
        vec4 v = model.vert(face, vert);                          // current vertex in object coordinates
        vec4 n = model.normal(face, vert);
        vec4 gl_Position = ModelView * vec4{v.x, v.y, v.z, 1.};
        tri[vert] =  gl_Position;                            // in eye coordinates
        varying_uv[vert] = model.uv(face, vert);
        varying_norm[vert] = ModelView.invert_transpose() * model.normal(face, vert);
        return Perspective * gl_Position;                         // in clip coordinates
    }

    virtual std::pair<bool,TGAColor> fragment(const vec3 bar) const {
        mat<2,4> E = {tri[1] - tri[0], tri[2] - tri[0]};
        mat<2,2> U = {varying_uv[1] - varying_uv[0], varying_uv[2] - varying_uv[0]};
        mat<2,4> T = U.invert() * E;
        mat<4,4> tanBaseTran = {normalized(T[0]),
                                normalized(T[1]),
                                normalized(varying_norm[0] * bar[0] + varying_norm[1] * bar[1] + varying_norm[2] * bar[2]),
                                {0, 0, 0, 1.}};

        vec2 uv = varying_uv[0] * bar[0] + varying_uv[1] * bar[1] + varying_uv[2] * bar[2];
        vec4 n = normalized(tanBaseTran.transpose() * model.normal(uv));
        vec4 r = normalized(2*n*(n*l) - l);   // 反射光向量
        vec4 fragPos = tri[0] * bar.x + tri[1] * bar.y + tri[2] * bar.z;
        
        vec4 viewDir = normalized(eye - fragPos);

        double ambient = 0.4; // 环境光
        double diffuse = std::max(0., n*l); // 漫反射强度
        double specularMask = sample2D(model.specular(), uv)[0] / 255.;
        double specular = specularMask * std::pow(std::max(0., r * viewDir), 32); // 镜面反射强度，因为相机在相机坐标系中的z轴正方向，所以取r.z作为视线方向与反射光的夹角余弦值
        double illumination = ambient + diffuse + specular;

        TGAColor fragColor = sample2D(model.diffuse(), uv);
        
        for(int channel = 0; channel < 3; channel++) {
            double value = fragColor[channel] * illumination;
            fragColor[channel] = static_cast<uint8_t>(std::clamp(value, 0.0, 255.0));
        }
        return {false, fragColor};                                    // do not discard the pixel
    }
};

int main(int argc, char** argv) {    
    // std::string filename = argc == 2 ? argv[1] : "obj/diablo3_pose/diablo3_pose.obj";
    // std::string filename = argc == 2 ? argv[1] : "obj/african_head/african_head.obj";

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " obj/model.obj" << std::endl;
        return 1;
    }

    constexpr int width  = 800;      // output image size
    constexpr int height = 800;
    constexpr vec3 light{ 1, 1, 1}; // light source 
    constexpr vec3    eye{ -1, 0, 2}; // camera position
    constexpr vec3 center{ 0, 0, 0}; // camera direction
    constexpr vec3     up{ 0, 1, 0}; // camera up vector

    lookat(eye, center, up);                                   // build the ModelView   matrix
    init_perspective(norm(eye-center));                        // build the Perspective matrix
    init_viewport(width/16, height/16, width*7/8, height*7/8); // build the Viewport    matrix
    init_zbuffer(width, height);
    TGAImage framebuffer(width, height, TGAImage::RGB);
    
    for(int m = 1; m < argc; m++) {
        Model model(argv[m]);
        PhoneShader shader(light, model, eye);
        for (int f=0; f<model.nfaces(); f++) {      // iterate through all facets
            Triangle clip = { shader.vertex(f, 0),  // assemble the primitive
                                shader.vertex(f, 1),
                                shader.vertex(f, 2) };
            rasterize(clip, shader, framebuffer);   // rasterize the primitive
        }
    }

    framebuffer.write_tga_file("framebuffer.tga");
    return 0;
}

