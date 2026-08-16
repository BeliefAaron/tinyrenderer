#include <cstdlib>
#include "our_gl.h"
#include "model.h"
#include <string>
#include <random>

#define M_PI 3.14159265358979323846

extern mat<4,4> Viewport, ModelView, Perspective; // "OpenGL" state matrices and
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
        double specular = specularMask * std::pow(std::max(0., r * viewDir), 32); // 镜面反射强度，因为相机在相机坐标系中的z轴正方向，所以取r * viewDir作为视线方向与反射光的夹角余弦值
        double illumination = ambient + diffuse + specular;

        TGAColor fragColor = sample2D(model.diffuse(), uv);
        
        for(int channel = 0; channel < 3; channel++) {
            double value = fragColor[channel] * illumination;
            fragColor[channel] = static_cast<uint8_t>(std::clamp(value, 0.0, 255.0));
        }
        return {false, fragColor};                                    // do not discard the pixel
    }
};

struct BlankShader : IShader {
    const Model &model;

    BlankShader(const Model &m) : model(m) {
    }

    virtual vec4 vertex(const int face, const int vert) {
        vec4 gl_Position = ModelView * model.vert(face, vert);
        return Perspective * gl_Position;
    }

    virtual std::pair<bool,TGAColor> fragment(const vec3 bar) const {
        return {false, {255, 255, 255, 255}};
    }
};

void draw_zbuffer(std::string filename, std::vector<double> &zbuffer, int width, int height) {
    TGAImage zImg(width, height, TGAImage::GRAYSCALE, {0,0,0,0});
    double minZ = 1000, maxZ = -1000;
    for(int x = 0; x < width; x++) {
        for(int y = 0; y < height; y++) {
            if(zbuffer[x + y * width] <= -999.0) continue;
            minZ = std::min(minZ, zbuffer[x + y * width]);
            maxZ = std::max(maxZ, zbuffer[x + y * width]);
        }
    }

    for(int x = 0; x < width; x++) {
        for(int y = 0; y < height; y++) {
            double z = zbuffer[x + y * width];
            if(z <= -999.0) continue;
            z = (z - minZ) / (maxZ - minZ) * 255;
            z = std::clamp(z, 0.0, 255.0);
            auto gray = static_cast<uint8_t>(z);
            TGAColor color = {};
            color[0] = gray;
            zImg.set(x, y, color);
        }
    }
    zImg.write_tga_file(filename);
}

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

    /** 
     * usual rendering
     */
    lookat(eye, center, up);                                   
    init_perspective(norm(eye-center));                        
    init_viewport(width/16, height/16, width*7/8, height*7/8); 
    init_zbuffer(width, height);
    TGAImage framebuffer(width, height, TGAImage::RGB, {177, 195, 209, 255});
    
    for(int m = 1; m < argc; m++) {
        Model model(argv[m]);
        // PhoneShader shader(light, model, eye);
        BlankShader shader(model);
        for (int f=0; f<model.nfaces(); f++) {      
            Triangle clip = { shader.vertex(f, 0),  
                                shader.vertex(f, 1),
                                shader.vertex(f, 2) };
            rasterize(clip, shader, framebuffer);   
        }
    }

    /**
     * SSAO
     */
    constexpr double aoRadius = .1;
    constexpr int samples = 128;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> dist(-aoRadius, aoRadius);

    auto smoothstep = [](double edge0, double edge1, double x) {         
            double t = std::clamp((x - edge0)/(edge1 - edge0), 0., 1.);  
            return t*t*(3 - 2*t);                                        // Hermite interpolation inbetween. The derivative of the smoothstep function is zero at both edges.
    };

#pragma omp parallel for
    for(int x = 0; x < width; x++) {
        for(int y = 0; y < height; y++) {
            double z = zbuffer[x + y*width];
            if(z < -100) continue;
            vec4 p = Viewport.invert() * vec4(x, y, z, 1.);  // for each pixel, project to object coordinate system 
            double vote = 0;
            double voters = 0;
            for(int i = 0; i < samples; i++) {
                vec4 sample = Viewport * (p + vec4(dist(gen), dist(gen), dist(gen), 0));    // then get samples randomly
                if(sample.x < 0 || sample.y < 0 || sample.x >= width || sample.y >=height) continue;
                double zp = zbuffer[(int)sample.x + (int)sample.y * width]; // get the scene's nearest depth at sample location from zbuffer
                if(z + 5 * aoRadius < zp) continue;                       // range check to remove the dark halo
                voters++;
                vote += zp > sample.z;  // get masked
            }
            // calculate ssao value and apply it on color
            double ssao = 1.0;
            if(voters > 0) {
                double occlusion = vote / voters * 0.4;
                ssao = smoothstep(0, 1, 1 - occlusion);
            }
            TGAColor c = framebuffer.get(x, y);
            c[0] *= ssao; c[1] *= ssao; c[2] *= ssao;
            framebuffer.set(x, y, c);
        }
    }

    framebuffer.write_tga_file("framebuffer.tga");

    return 0;
}

