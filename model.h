#include <vector>
#include "geometry.h"
#include "tgaimage.h"

class Model {
    std::vector<vec4> verts = {};    // array of vertices
    std::vector<vec4> norms = {};    // array of normal vectors
    std::vector<vec2> tex = {};      // array of texture
    std::vector<int> facet_vrt = {}; // per-triangle index in the above array
    std::vector<int> facet_norm = {};
    std::vector<int> facet_tex = {};
    TGAImage normalMap = {};       // normal map texture
    TGAImage diffuseMap = {};
    TGAImage specularMap = {};

public:
    Model(const std::string filename);
    int nverts() const; // number of vertices
    int nfaces() const; // number of triangles
    vec4 vert(const int i) const;                          // 0 <= i < nverts()
    vec4 vert(const int iface, const int nthvert) const;   // 0 <= iface <= nfaces(), 0 <= nthvert < 3
    vec4 normal(const int iface, const int nthvert) const; // normal coming from "vn"
    vec4 normal(const vec2& uv) const;                     // normal coming from normal map texture
    vec2 uv(const int iface, const int nthvert) const;
    const TGAImage& diffuse() const;
    const TGAImage& specular() const;
};