#include "Utils/ColorPipelineCPU.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <thread>

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

void convertRawToRGB24(const uint16_t* raw, const CPUColorParams& p,
                       std::vector<uint8_t>& outRGB, unsigned threads)
{
    outRGB.resize(p.width * p.height * 3);
    double range = p.whiteLevel - p.blackLevel;
    double invRange = (std::abs(range) < 1e-6) ? 1.0 : 1.0 / range;

    auto lin = [&](int x, int y) -> float {
        return linFromRaw(readU16(raw,x,y,p.width,p.height), p.blackLevel, invRange);
    };
    auto vhEdge = [&](int x, int y){
        float v=0.0f,h=0.0f;
        for(int i=-1;i<=1;++i){
            v += lin(x+i,y-3) - 3.0f*lin(x+i,y-2) - lin(x+i,y-1) + 6.0f*lin(x+i,y) - lin(x+i,y+1) - 3.0f*lin(x+i,y+2) + lin(x+i,y+3);
            h += lin(x-3,y+i) - 3.0f*lin(x-2,y+i) - lin(x-1,y+i) + 6.0f*lin(x,y+i) - lin(x+1,y+i) - 3.0f*lin(x+2,y+i) + lin(x+3,y+i);
        }
        v*=v; h*=h;
        return v / (1e-5f + v + h);
    };
    auto interpGreen = [&](int x,int y){
        float vhVal = vhEdge(x,y);
        float vhNbr = 0.25f*(vhEdge(x-1,y-1)+vhEdge(x+1,y-1)+vhEdge(x-1,y+1)+vhEdge(x+1,y+1));
        float vhDiscr = (std::abs(0.5f-vhVal) < std::abs(0.5f-vhNbr)) ? vhNbr : vhVal;
        float eps=1e-5f;
        float nGrad = eps + std::abs(lin(x,y-1)-lin(x,y+1)) + std::abs(lin(x,y)-lin(x,y-2));
        float sGrad = eps + std::abs(lin(x,y+1)-lin(x,y-1)) + std::abs(lin(x,y)-lin(x,y+2));
        float eGrad = eps + std::abs(lin(x-1,y)-lin(x+1,y)) + std::abs(lin(x,y)-lin(x-2,y));
        float wGrad = eps + std::abs(lin(x+1,y)-lin(x-1,y)) + std::abs(lin(x,y)-lin(x+2,y));
        float gV = (sGrad*lin(x,y-1) + nGrad*lin(x,y+1))/(nGrad+sGrad);
        float gH = (eGrad*lin(x-1,y) + wGrad*lin(x+1,y))/(eGrad+wGrad);
        return gV*(1.0f-vhDiscr) + gH*vhDiscr;
    };
    auto interpHoriz = [&](int x,int y){ return 0.5f*(lin(x+1,y)+lin(x-1,y)); };
    auto interpVert  = [&](int x,int y){ return 0.5f*(lin(x,y+1)+lin(x,y-1)); };
    auto interpDiag  = [&](int x,int y){ return 0.25f*(lin(x+1,y+1)+lin(x-1,y+1)+lin(x+1,y-1)+lin(x-1,y-1)); };

    const float* ccm = p.ccm.data();

    auto processRow = [&](int y){
        for(int x=0;x<p.width;++x){
            bool ye = (y%2)==0;
            bool xe = (x%2)==0;
            float r=0,g=0,b=0;
            switch(p.cfaType){
                case 0: // BGGR
                    if(ye){
                        if(xe){ b=lin(x,y); g=interpGreen(x,y); r=interpDiag(x,y); }
                        else { g=lin(x,y); r=interpVert(x,y); b=interpHoriz(x,y); }
                    }else{
                        if(xe){ g=lin(x,y); r=interpHoriz(x,y); b=interpVert(x,y); }
                        else { r=lin(x,y); g=interpGreen(x,y); b=interpDiag(x,y); }
                    }
                    break;
                case 1: // RGGB
                    if(ye){
                        if(xe){ r=lin(x,y); g=interpGreen(x,y); b=interpDiag(x,y); }
                        else { g=lin(x,y); r=interpHoriz(x,y); b=interpVert(x,y); }
                    }else{
                        if(xe){ g=lin(x,y); r=interpVert(x,y); b=interpHoriz(x,y); }
                        else { b=lin(x,y); g=interpGreen(x,y); r=interpDiag(x,y); }
                    }
                    break;
                case 2: // GBRG
                    if(ye){
                        if(xe){ g=lin(x,y); r=interpVert(x,y); b=interpHoriz(x,y); }
                        else { b=lin(x,y); g=interpGreen(x,y); r=interpDiag(x,y); }
                    }else{
                        if(xe){ r=lin(x,y); g=interpGreen(x,y); b=interpDiag(x,y); }
                        else { g=lin(x,y); r=interpHoriz(x,y); b=interpVert(x,y); }
                    }
                    break;
                default: // GRBG
                    if(ye){
                        if(xe){ g=lin(x,y); r=interpHoriz(x,y); b=interpVert(x,y); }
                        else { r=lin(x,y); g=interpGreen(x,y); b=interpDiag(x,y); }
                    }else{
                        if(xe){ b=lin(x,y); g=interpGreen(x,y); r=interpDiag(x,y); }
                        else { g=lin(x,y); r=interpVert(x,y); b=interpHoriz(x,y); }
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
    };

    if(threads <= 1) {
        for(int y=0;y<p.height;++y)
            processRow(y);
    } else {
        std::vector<std::thread> workers;
        workers.reserve(threads);
        for(unsigned t=0;t<threads;++t){
            workers.emplace_back([&,t]{
                for(int y=t;y<p.height;y+=threads)
                    processRow(y);
            });
        }
        for(auto& th : workers) th.join();
    }
}
