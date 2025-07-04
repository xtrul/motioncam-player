#include "Utils/ColorPipelineCPU.h"
#include <algorithm>
#include <cmath>
#ifdef USE_OPENMP
#include <omp.h>
#endif

static inline float srgb_eotf(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    return (v <= 0.0031308f) ? v * 12.92f : 1.055f * std::pow(v, 1.0f/2.4f) - 0.055f;
}

static inline uint16_t readU16(const uint16_t* src, int x, int y, int w, int h) {
    x = std::clamp(x,0,w-1);
    y = std::clamp(y,0,h-1);
    return src[y*w + x];
}

static inline float linFromRaw(uint16_t v, double black, double invRange) {
    double t = (static_cast<double>(v) - black) * invRange;
    t = std::clamp(t, 0.0, 1.0);
    return static_cast<float>(t);
}

void convertRawToRGB24(const uint16_t* raw, const CPUColorParams& p, std::vector<uint8_t>& outRGB)
{
    outRGB.resize(p.width * p.height * 3);
    double range = p.whiteLevel - p.blackLevel;
    double invRange = (std::abs(range) < 1e-6) ? 1.0 : 1.0 / range;

    auto lin = [&](int x, int y) -> float {
        return linFromRaw(readU16(raw,x,y,p.width,p.height), p.blackLevel, invRange);
    };
    auto interpG = [&](int x, int y)->float{ return 0.25f*(lin(x+1,y)+lin(x-1,y)+lin(x,y+1)+lin(x,y-1));};
    auto interpH = [&](int x, int y)->float{ return 0.5f*(lin(x+1,y)+lin(x-1,y));};
    auto interpV = [&](int x, int y)->float{ return 0.5f*(lin(x,y+1)+lin(x,y-1));};
    auto interpD = [&](int x, int y)->float{ return 0.25f*(lin(x+1,y+1)+lin(x-1,y+1)+lin(x+1,y-1)+lin(x-1,y-1));};

    const float* ccm = p.ccm.data();

    #ifdef USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for(int y=0;y<p.height;++y){
        for(int x=0;x<p.width;++x){
            bool ye = (y%2)==0;
            bool xe = (x%2)==0;
            float r=0,g=0,b=0;
            switch(p.cfaType){
                case 0: // BGGR
                    if(ye){
                        if(xe){ b=lin(x,y); g=interpG(x,y); r=interpD(x,y); }
                        else { g=lin(x,y); r=interpV(x,y); b=interpH(x,y); }
                    }else{
                        if(xe){ g=lin(x,y); r=interpH(x,y); b=interpV(x,y); }
                        else { r=lin(x,y); g=interpG(x,y); b=interpD(x,y); }
                    }
                    break;
                case 1: // RGGB
                    if(ye){
                        if(xe){ r=lin(x,y); g=interpG(x,y); b=interpD(x,y); }
                        else { g=lin(x,y); r=interpH(x,y); b=interpV(x,y); }
                    }else{
                        if(xe){ g=lin(x,y); r=interpV(x,y); b=interpH(x,y); }
                        else { b=lin(x,y); g=interpG(x,y); r=interpD(x,y); }
                    }
                    break;
                case 2: // GBRG
                    if(ye){
                        if(xe){ g=lin(x,y); r=interpV(x,y); b=interpH(x,y); }
                        else { b=lin(x,y); g=interpG(x,y); r=interpD(x,y); }
                    }else{
                        if(xe){ r=lin(x,y); g=interpG(x,y); b=interpD(x,y); }
                        else { g=lin(x,y); r=interpH(x,y); b=interpV(x,y); }
                    }
                    break;
                default: // GRBG
                    if(ye){
                        if(xe){ g=lin(x,y); r=interpH(x,y); b=interpV(x,y); }
                        else { r=lin(x,y); g=interpG(x,y); b=interpD(x,y); }
                    }else{
                        if(xe){ b=lin(x,y); g=interpG(x,y); r=interpD(x,y); }
                        else { g=lin(x,y); r=interpV(x,y); b=interpH(x,y); }
                    }
                    break;
            }
            float r_wb = std::clamp(r * p.gainR, 0.0f, 1.0f);
            float g_wb = std::clamp(g * p.gainG, 0.0f, 1.0f);
            float b_wb = std::clamp(b * p.gainB, 0.0f, 1.0f);
            float r_cc = ccm[0]*r_wb + ccm[3]*g_wb + ccm[6]*b_wb;
            float g_cc = ccm[1]*r_wb + ccm[4]*g_wb + ccm[7]*b_wb;
            float b_cc = ccm[2]*r_wb + ccm[5]*g_wb + ccm[8]*b_wb;
            r_cc = std::clamp(r_cc,0.0f,1.0f);
            g_cc = std::clamp(g_cc,0.0f,1.0f);
            b_cc = std::clamp(b_cc,0.0f,1.0f);
            float lum = 0.2126f*r_cc + 0.7152f*g_cc + 0.0722f*b_cc;
            float sat = p.saturation;
            r_cc = lum*(1-sat) + r_cc*sat;
            g_cc = lum*(1-sat) + g_cc*sat;
            b_cc = lum*(1-sat) + b_cc*sat;
            uint8_t* dst = &outRGB[(y*p.width + x)*3];
            dst[0] = (uint8_t)std::clamp(int(srgb_eotf(r_cc)*255.0f + 0.5f),0,255);
            dst[1] = (uint8_t)std::clamp(int(srgb_eotf(g_cc)*255.0f + 0.5f),0,255);
            dst[2] = (uint8_t)std::clamp(int(srgb_eotf(b_cc)*255.0f + 0.5f),0,255);
        }
    }
}
