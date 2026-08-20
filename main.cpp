#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "our_gl.h"
#include "model.h"

#define M_PI 3.14159265358979323846

extern mat<4,4> Viewport, ModelView, Perspective; // state matrices
extern std::vector<double> zbuffer;     // the depth buffer

struct PhoneShader : IShader {
    const Model &model;
    vec4 l;       // light direction in eye coordinates
    vec4 tri[3];  // triangle in eye coordinates
    vec4 eye;     // camera position in eye coordinates
    vec2 varying_uv[3];
    vec3 varying_geom_norm[3];
    mat<4,4> normalMatrix;

    PhoneShader(const vec3 &light, const Model &m, const vec3 &e) : model(m) {
        l = normalized((ModelView*vec4{light.x, light.y, light.z, 0.}));  // transform the light vector to view coordinates
        eye = (ModelView * vec4{e.x, e.y, e.z, 1.});
        normalMatrix = ModelView.invert_transpose();
    }

    virtual vec4 vertex(const int face, const int vert) {
        vec4 v = model.vert(face, vert);                          // current vertex in object coordinates
        vec4 gl_Position = ModelView * vec4{v.x, v.y, v.z, 1.};
        tri[vert] =  gl_Position;                            // in eye coordinates
        varying_uv[vert] = model.uv(face, vert);
        varying_geom_norm[vert] = normalized((normalMatrix * model.normal(face, vert)).xyz());
        return Perspective * gl_Position;                         // in clip coordinates
    }

    FragmentOutput fragment(const vec3 bar) const {
        vec3 aoNormal = normalized(varying_geom_norm[0] * bar[0]
                                 + varying_geom_norm[1] * bar[1]
                                 + varying_geom_norm[2] * bar[2]);

        mat<2,4> E = {tri[1] - tri[0], tri[2] - tri[0]};
        mat<2,2> U = {varying_uv[1] - varying_uv[0], varying_uv[2] - varying_uv[0]};
        mat<2,4> T = U.invert() * E;
        mat<4,4> tanBaseTran = {normalized(T[0]),
                                normalized(T[1]),
                                {aoNormal.x, aoNormal.y, aoNormal.z, 0},
                                {0, 0, 0, 1.}};

        vec2 uv = varying_uv[0] * bar[0] + varying_uv[1] * bar[1] + varying_uv[2] * bar[2];
        vec4 n = normalized(tanBaseTran.transpose() * model.normal(uv));
        vec4 r = normalized(2*n*(n*l) - l);
        vec4 fragPos = tri[0] * bar.x + tri[1] * bar.y + tri[2] * bar.z;
        vec4 viewDir = normalized(eye - fragPos);

        // A visible surface should expose its outward hemisphere to the camera.
        if (aoNormal * viewDir.xyz() < 0) aoNormal = aoNormal * -1.;

        double ambient = 0.4;
        double diffuse = std::max(0., n*l);
        double specularMask = sample2D(model.specular(), uv)[0] / 255.;
        // double specular = specularMask * std::pow(std::max(0., r * viewDir), 32);   // Phong 光照模型
        vec4 halfDir = normalized(l + viewDir);
        double specular = specularMask * std::pow(std::max(0., n * halfDir), 32);   // Blinn-Phong 光照模型  
        
        TGAColor texel = sample2D(model.diffuse(), uv);
        vec3 albedo = {     // 反射率(物体固有颜色)
            static_cast<double>(texel[0]),
            static_cast<double>(texel[1]),
            static_cast<double>(texel[2])
        };

        FragmentOutput output;
        output.color = texel;
        output.viewPosition = fragPos.xyz();
        output.aoNormal = aoNormal;
        output.ambientColor = albedo * ambient;
        output.directColor = albedo * (diffuse + specular);
        output.writeGeometry = true;
        return output;
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

    virtual FragmentOutput fragment(const vec3) const {
        FragmentOutput output;
        output.color = {255, 255, 255, 255};
        return output;
    }
};

std::vector<vec3> make_ssao_kernel(const int samples) {
    std::mt19937 generator(0x5A17u);
    std::uniform_real_distribution<double> random01(0., 1.);
    std::vector<vec3> kernel;
    kernel.reserve(samples);    

    for (int i=0; i<samples; i++) {
        const double u1 = random01(generator);
        const double u2 = random01(generator);
        const double r = std::sqrt(u1);     // 让更多样本集中在半球法线附近
        const double phi = 2.*M_PI*u2;
        vec3 sample = {r*std::cos(phi), r*std::sin(phi), std::sqrt(1.-u1)};

        const double t = static_cast<double>(i+1)/samples;  // 使采样点近密远疏
        const double scale = .1 + .9*t*t;                   // 提高近距离遮挡的贡献
        kernel.push_back(sample*scale);
    }
    return kernel;
}

// 随机旋转ssao采样核，让每个pixel有不同的采样旋转
double pixel_rotation(const int x, const int y) {
    std::uint32_t hash = static_cast<std::uint32_t>(x)*0x8da6b343u
                       ^ static_cast<std::uint32_t>(y)*0xd8163841u;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    return (static_cast<double>(hash)/static_cast<double>(UINT32_MAX))*2.*M_PI;
}

double smoothstep(const double edge0, const double edge1, const double x) {
    const double t = std::clamp((x-edge0)/(edge1-edge0), 0., 1.);
    return t*t*(3.-2.*t);
}

void draw_zbuffer(const std::string &filename, const std::vector<double> &depthBuffer, const int width, const int height) {
    TGAImage zImg(width, height, TGAImage::GRAYSCALE, {0,0,0,0});
    double minZ = 1000, maxZ = -1000;
    for(int x = 0; x < width; x++) {
        for(int y = 0; y < height; y++) {
            if(depthBuffer[x + y * width] <= -999.0) continue;
            minZ = std::min(minZ, depthBuffer[x + y * width]);
            maxZ = std::max(maxZ, depthBuffer[x + y * width]);
        }
    }

    for(int x = 0; x < width; x++) {
        for(int y = 0; y < height; y++) {
            double z = depthBuffer[x + y * width];
            if(z <= -999.0) continue;
            z = maxZ > minZ ? (z - minZ) / (maxZ - minZ) * 255. : 255.;
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

    constexpr int width = 800;
    constexpr int height = 800;
    constexpr int shadowWidth = 2048;
    constexpr int shadowHeight = 2048;
    constexpr vec3 lightDirection{1, 1, 1};
    constexpr vec3 eye{-1, 0, 2};
    constexpr vec3 center{0, 0, 0};
    constexpr vec3 up{0, 1, 0};

    std::vector<Model> models;
    models.reserve(argc - 1);
    for (int m = 1; m < argc; m++) {
        models.emplace_back(argv[m]);
    }

    /**
     * Shadow pass
     */
    // Render an orthographic depth map from the directional light.
    lookat(lightDirection, center, up);
    init_orthographic();
    init_viewport(0, 0, shadowWidth, shadowHeight);
    init_zbuffer(shadowWidth, shadowHeight);

    const mat<4,4> lightModelView = ModelView;
    const mat<4,4> lightPerspective = Perspective;
    const mat<4,4> lightViewport = Viewport;
    TGAImage shadowFramebuffer(shadowWidth, shadowHeight, TGAImage::GRAYSCALE, {0, 0, 0, 0});

    for (const Model &model : models) {
        BlankShader shader(model);
        for (int f = 0; f < model.nfaces(); f++) {
            Triangle clip = {
                shader.vertex(f, 0),
                shader.vertex(f, 1),
                shader.vertex(f, 2)
            };
            rasterize(clip, shader, shadowFramebuffer, nullptr, false);
        }
    }

    const std::vector<double> shadowDepth = zbuffer;
    // draw_zbuffer("shadow_depth.tga", shadowDepth, shadowWidth, shadowHeight);
    draw_zbuffer("shadow_zbuffer.tga", shadowDepth, shadowWidth, shadowHeight);


    /**
     * usual render(camera pass)
     */
    // Restore camera state and fill the geometry/material buffers.
    lookat(eye, center, up);
    init_perspective(norm(eye-center));
    init_viewport(width/16, height/16, width*7/8, height*7/8);
    init_zbuffer(width, height);

    const mat<4,4> cameraModelView = ModelView;
    const mat<4,4> cameraModelViewInv = cameraModelView.invert();
    const vec3 lightDirectionInCameraView = normalized(
        (cameraModelView * vec4{lightDirection.x, lightDirection.y, lightDirection.z, 0.}).xyz()
    );

    TGAImage framebuffer(width, height, TGAImage::RGB, {177, 195, 209, 255});
    GBuffer gbuffer(width, height);
    for (const Model &model : models) {
        PhoneShader shader(lightDirection, model, eye);
        for (int f = 0; f < model.nfaces(); f++) {
            Triangle clip = {
                shader.vertex(f, 0),
                shader.vertex(f, 1),
                shader.vertex(f, 2)
            };
            rasterize(clip, shader, framebuffer, &gbuffer);
        }
    }

    /**
     * SSAO
     */
    constexpr double aoRadius = .12;
    constexpr double aoBias = .01;
    constexpr double aoStrength = 1.;
    constexpr int samples = 128;
    const std::vector<vec3> kernel = make_ssao_kernel(samples);
    std::vector<double> aoBuffer(width*height, 1.); // store ao value(visibility)

#pragma omp parallel for
    for(int x = 0; x < width; x++) {
        for(int y = 0; y < height; y++) {
            const int index = x + y * width;
            if (!gbuffer.valid[index]) continue;

            const vec3 p = gbuffer.viewPosition[index];
            const vec3 n = normalized(gbuffer.viewNormal[index]);
            const vec3 helper = std::abs(n.z) < .999 ? vec3{0, 0, 1} : vec3{0, 1, 0};   // in case n = (0,0,1)
            const vec3 tangent = normalized(cross(helper, n));
            const vec3 bitangent = cross(n, tangent);   // build TBN for sample local coordinate to fragment's view space
            const double angle = pixel_rotation(x, y);
            const double cosAngle = std::cos(angle);
            const double sinAngle = std::sin(angle);

            double occlusion = 0.;
            int voters = 0;
            for(const vec3 &localSample : kernel) {
                const double rotatedX = localSample.x*cosAngle - localSample.y*sinAngle;
                const double rotatedY = localSample.x*sinAngle + localSample.y*cosAngle;
                const vec3 sampleDirection = tangent*rotatedX + bitangent*rotatedY + n*localSample.z;
                const vec3 samplePosition = p + sampleDirection*aoRadius;

                const vec4 clip = Perspective * vec4{samplePosition.x, samplePosition.y, samplePosition.z, 1.};
                if (clip.w <= 1e-8) continue;   
                const vec4 screen = Viewport * (clip/clip.w);
                const int sampleX = static_cast<int>(std::round(screen.x));
                const int sampleY = static_cast<int>(std::round(screen.y));
                if(sampleX < 0 || sampleY < 0 || sampleX >= width || sampleY >= height) continue;

                const int sampleIndex = sampleX + sampleY*width;
                if (!gbuffer.valid[sampleIndex]) continue;

                const vec3 visiblePosition = gbuffer.viewPosition[sampleIndex];
                const double dz = std::abs(visiblePosition.z-p.z);
                if (dz > aoRadius) continue;
                
                voters++;
                
                const double rangeWeight = smoothstep(0., 1., aoRadius/std::max(dz, 1e-6)); // 降低深度差较大的样本对结果的影响（深度差大的遮蔽效果差）
                if (visiblePosition.z > samplePosition.z + aoBias) {
                    occlusion += rangeWeight;
                }
            }

            const double visibility = voters > 0 ? 1.-occlusion/voters : 1.;
            aoBuffer[index] = std::pow(std::clamp(visibility, 0., 1.), aoStrength);
        }
    }

    TGAImage aoImage(width, height, TGAImage::GRAYSCALE, {255, 255, 255, 255});
#pragma omp parallel for
    for(int x = 0; x < width; x++) {
        for(int y = 0; y < height; y++) {
            const int index = x + y*width;
            const double ssao = aoBuffer[index];
            TGAColor aoColor = {};
            aoColor[0] = static_cast<std::uint8_t>(std::clamp(ssao*255., 0., 255.));
            aoImage.set(x, y, aoColor);
        }
    }

    /**
     * Shadow Mapping
     */
    // Project camera-visible positions into the light depth map and apply 3x3 PCF.
    constexpr double minShadowBias = .002;
    constexpr double slopeShadowBias = .02;
    std::vector<double> shadowBuffer(width*height, 1.);
    TGAImage shadowFactorImage(width, height, TGAImage::GRAYSCALE, {255, 255, 255, 255});

#pragma omp parallel for
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            const int index = x + y * width;
            if (!gbuffer.valid[index]) continue;

            const vec3 cameraPosition = gbuffer.viewPosition[index];
            const vec4 worldPosition = cameraModelViewInv * vec4{
                cameraPosition.x, cameraPosition.y, cameraPosition.z, 1.
            };
            const vec4 lightClip = lightPerspective * lightModelView * worldPosition;
            if (lightClip.w <= 1e-8) continue;

            const vec4 lightNdc = lightClip / lightClip.w;
            if (lightNdc.x < -1. || lightNdc.x > 1. ||
                lightNdc.y < -1. || lightNdc.y > 1.) continue;

            const vec4 lightScreen = lightViewport * lightNdc;
            const int shadowX = static_cast<int>(std::round(lightScreen.x));
            const int shadowY = static_cast<int>(std::round(lightScreen.y));
            const double receiverDepth = lightScreen.z; // 相机(观察空间)看到的表面在光源视角下的深度
            // calculate shadow bias
            const double ndotl = std::abs(gbuffer.viewNormal[index] * lightDirectionInCameraView);
            const double bias = std::max(minShadowBias, slopeShadowBias * (1. - ndotl));

            // use 3x3 PCF to make shadow edge softly
            double visibility = 0.;
            int taps = 0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    const int sx = shadowX + dx;
                    const int sy = shadowY + dy;
                    if (sx < 0 || sy < 0 || sx >= shadowWidth || sy >= shadowHeight) continue;

                    const double blockerDepth = shadowDepth[sx + sy*shadowWidth];  
                    if (blockerDepth <= -999. || receiverDepth >= blockerDepth - bias) {    // 接受光照
                        visibility += 1.;
                    }
                    taps++;
                }
            }

            const double shadowFactor = taps > 0 ? visibility/taps : 1.;
            shadowBuffer[index] = shadowFactor;
            TGAColor shadowColor = {};
            shadowColor[0] = static_cast<std::uint8_t>(
                std::clamp(shadowFactor*255., 0., 255.)
            );
            shadowFactorImage.set(x, y, shadowColor);
        }
    }

    /**
     * illumination Synthesis
     */
    // SSAO modulates ambient light; the shadow map modulates only direct light.
#pragma omp parallel for
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            const int index = x + y * width;
            if (!gbuffer.valid[index]) continue;

            const vec3 finalColor = gbuffer.ambientColor[index] * aoBuffer[index] + gbuffer.directColor[index] * shadowBuffer[index];
            TGAColor output = {};
            for (int channel = 0; channel < 3; channel++) {
                output[channel] = static_cast<std::uint8_t>(std::clamp(finalColor[channel], 0., 255.));
            }
            framebuffer.set(x, y, output);
        }
    }

    aoImage.write_tga_file("ssao.tga");
    shadowFactorImage.write_tga_file("shadow_factor.tga");
    framebuffer.write_tga_file("framebuffer.tga");

    return 0;
}

