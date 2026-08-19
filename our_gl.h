#include "tgaimage.h"
#include "geometry.h"
#include "algorithm"

void lookat(const vec3 eye, const vec3 center, const vec3 up);
void init_perspective(const double f);
void init_orthographic();
void init_viewport(const int x, const int y, const int w, const int h);
void init_zbuffer(const int width, const int height);

struct GBuffer {
    std::vector<vec3> viewPosition;
    std::vector<vec3> viewNormal;
    std::vector<vec3> ambientColor;
    std::vector<vec3> directColor;
    std::vector<std::uint8_t> valid;    // 几何体是否有效

    GBuffer(int width, int height)
        : viewPosition(width * height),
          viewNormal(width * height),
          ambientColor(width * height),
          directColor(width * height),
          valid(width * height, 0) {}
};

struct FragmentOutput {
    bool discard = false;
    TGAColor color = {};
    vec3 viewPosition = {};
    vec3 aoNormal = {};
    vec3 ambientColor = {};
    vec3 directColor = {};
    bool writeGeometry = false;
};

struct IShader {
    static TGAColor sample2D (const TGAImage &img, const vec2 &uv) {
        if(img.width() == 0 || img.height() == 0) return {};

        int x = static_cast<int>(std::clamp(uv.x, 0.0, 1.0) * (img.width() - 1));
        int y = static_cast<int>(std::clamp(uv.y, 0.0, 1.0) * (img.height() - 1));
        
        return img.get(x, y);
    }
    // virtual std::pair<bool,TGAColor> fragment(const vec3 bar) const = 0;
    virtual FragmentOutput fragment(const vec3 bar) const = 0;
};

typedef vec4 Triangle[3]; // a triangle primitive is made of three ordered points
void rasterize(const Triangle &clip, const IShader &shader, TGAImage &framebuffer,
               GBuffer *gbuffer = nullptr, bool cullBackFaces = true);

