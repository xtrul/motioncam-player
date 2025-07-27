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
    auto vh = [&](int x, int y)->float {
        float xs = 0.0f, ys = 0.0f;
        for(int i=-1;i<=1;++i){
            xs += lin(x+i,y-3) - 3.0f*lin(x+i,y-2) - lin(x+i,y-1) + 6.0f*lin(x+i,y)
                 - lin(x+i,y+1) - 3.0f*lin(x+i,y+2) + lin(x+i,y+3);
            ys += lin(x-3,y+i) - 3.0f*lin(x-2,y+i) - lin(x-1,y+i) + 6.0f*lin(x,y+i)
                 - lin(x+1,y+i) - 3.0f*lin(x+2,y+i) + lin(x+3,y+i);
        }
        xs *= xs; ys *= ys;
        return xs / (1e-5f + xs + ys);
    };

    auto greenAtRB = [&](int x, int y)->float {
        float vhVal = vh(x,y);
        float vhNeigh = 0.25f*(vh(x-1,y-1)+vh(x+1,y-1)+vh(x-1,y+1)+vh(x+1,y+1));
        float vhDiscr = std::abs(0.5f - vhVal) < std::abs(0.5f - vhNeigh) ? vhNeigh : vhVal;
        float eps = 1e-5f;
        float N_grad = eps + std::abs(lin(x, y-1) - lin(x, y+1)) + std::abs(lin(x, y) - lin(x, y-2));
        float S_grad = eps + std::abs(lin(x, y-1) - lin(x, y+1)) + std::abs(lin(x, y) - lin(x, y+2));
        float E_grad = eps + std::abs(lin(x-1, y) - lin(x+1, y)) + std::abs(lin(x, y) - lin(x+2, y));
        float W_grad = eps + std::abs(lin(x-1, y) - lin(x+1, y)) + std::abs(lin(x, y) - lin(x-2, y));
        float g_v = (S_grad * (lin(x, y-1)+lin(x, y+1))*0.5f + N_grad * (lin(x, y-1)+lin(x, y+1))*0.5f)/(N_grad + S_grad);
        float g_h = (E_grad * (lin(x-1, y)+lin(x+1, y))*0.5f + W_grad * (lin(x-1, y)+lin(x+1, y))*0.5f)/(E_grad + W_grad);
        return g_v*(1.0f - vhDiscr) + g_h*vhDiscr;
    };

    auto greenAt = [&](int x, int y)->float {
        bool ye = (y%2)==0;
        bool xe = (x%2)==0;
        switch(p.cfaType){
            case 0: // BGGR
                return ye ? (xe ? greenAtRB(x,y) : lin(x,y))
                          : (xe ? lin(x,y) : greenAtRB(x,y));
            case 1: // RGGB
                return ye ? (xe ? lin(x,y) : greenAtRB(x,y))
                          : (xe ? greenAtRB(x,y) : lin(x,y));
            case 2: // GBRG
                return ye ? (xe ? lin(x,y) : greenAtRB(x,y))
                          : (xe ? greenAtRB(x,y) : lin(x,y));
            default: // GRBG
                return ye ? (xe ? greenAtRB(x,y) : lin(x,y))
                          : (xe ? lin(x,y) : greenAtRB(x,y));
        }
    };

    auto rcdPixel = [&](int x, int y, float& r, float& g, float& b){
        bool ye = (y%2)==0;
        bool xe = (x%2)==0;
        switch(p.cfaType){
            case 0: // BGGR
                if(ye){
                    if(xe){
                        g=greenAtRB(x,y); b=lin(x,y);
                        float dr=(lin(x-1,y-1)-greenAt(x-1,y-1)+lin(x+1,y-1)-greenAt(x+1,y-1)+lin(x-1,y+1)-greenAt(x-1,y+1)+lin(x+1,y+1)-greenAt(x+1,y+1))*0.25f;
                        r=g+dr;
                    }else{
                        g=lin(x,y);
                        float dr=(lin(x,y-1)-greenAt(x,y-1)+lin(x,y+1)-greenAt(x,y+1))*0.5f;
                        float db=(lin(x-1,y)-greenAt(x-1,y)+lin(x+1,y)-greenAt(x+1,y))*0.5f;
                        r=g+dr; b=g+db;
                    }
                }else{
                    if(xe){
                        g=lin(x,y);
                        float dr=(lin(x-1,y)-greenAt(x-1,y)+lin(x+1,y)-greenAt(x+1,y))*0.5f;
                        float db=(lin(x,y-1)-greenAt(x,y-1)+lin(x,y+1)-greenAt(x,y+1))*0.5f;
                        r=g+dr; b=g+db;
                    }else{
                        g=greenAtRB(x,y); r=lin(x,y);
                        float db=(lin(x-1,y-1)-greenAt(x-1,y-1)+lin(x+1,y-1)-greenAt(x+1,y-1)+lin(x-1,y+1)-greenAt(x-1,y+1)+lin(x+1,y+1)-greenAt(x+1,y+1))*0.25f;
                        b=g+db;
                    }
                }
                break;
            case 1: // RGGB
                if(ye){
                    if(xe){
                        g=greenAtRB(x,y); r=lin(x,y);
                        float db=(lin(x-1,y-1)-greenAt(x-1,y-1)+lin(x+1,y-1)-greenAt(x+1,y-1)+lin(x-1,y+1)-greenAt(x-1,y+1)+lin(x+1,y+1)-greenAt(x+1,y+1))*0.25f;
                        b=g+db;
                    }else{
                        g=lin(x,y);
                        float dr=(lin(x-1,y)-greenAt(x-1,y)+lin(x+1,y)-greenAt(x+1,y))*0.5f;
                        float db=(lin(x,y-1)-greenAt(x,y-1)+lin(x,y+1)-greenAt(x,y+1))*0.5f;
                        r=g+dr; b=g+db;
                    }
                }else{
                    if(xe){
                        g=lin(x,y);
                        float dr=(lin(x,y-1)-greenAt(x,y-1)+lin(x,y+1)-greenAt(x,y+1))*0.5f;
                        float db=(lin(x-1,y)-greenAt(x-1,y)+lin(x+1,y)-greenAt(x+1,y))*0.5f;
                        r=g+dr; b=g+db;
                    }else{
                        g=greenAtRB(x,y); b=lin(x,y);
                        float dr=(lin(x-1,y-1)-greenAt(x-1,y-1)+lin(x+1,y-1)-greenAt(x+1,y-1)+lin(x-1,y+1)-greenAt(x-1,y+1)+lin(x+1,y+1)-greenAt(x+1,y+1))*0.25f;
                        r=g+dr;
                    }
                }
                break;
            case 2: // GBRG
                if(ye){
                    if(xe){
                        g=lin(x,y);
                        float db=(lin(x-1,y)-greenAt(x-1,y)+lin(x+1,y)-greenAt(x+1,y))*0.5f;
                        float dr=(lin(x,y-1)-greenAt(x,y-1)+lin(x,y+1)-greenAt(x,y+1))*0.5f;
                        b=g+db; r=g+dr;
                    }else{
                        g=greenAtRB(x,y); b=lin(x,y);
                        float dr=(lin(x-1,y-1)-greenAt(x-1,y-1)+lin(x+1,y-1)-greenAt(x+1,y-1)+lin(x-1,y+1)-greenAt(x-1,y+1)+lin(x+1,y+1)-greenAt(x+1,y+1))*0.25f;
                        r=g+dr;
                    }
                }else{
                    if(xe){
                        g=greenAtRB(x,y); r=lin(x,y);
                        float db=(lin(x-1,y-1)-greenAt(x-1,y-1)+lin(x+1,y-1)-greenAt(x+1,y-1)+lin(x-1,y+1)-greenAt(x-1,y+1)+lin(x+1,y+1)-greenAt(x+1,y+1))*0.25f;
                        b=g+db;
                    }else{
                        g=lin(x,y);
                        float dr=(lin(x-1,y)-greenAt(x-1,y)+lin(x+1,y)-greenAt(x+1,y))*0.5f;
                        float db=(lin(x,y-1)-greenAt(x,y-1)+lin(x,y+1)-greenAt(x,y+1))*0.5f;
                        r=g+dr; b=g+db;
                    }
                }
                break;
            default: // GRBG
                if(ye){
                    if(xe){
                        g=lin(x,y);
                        float dr=(lin(x-1,y)-greenAt(x-1,y)+lin(x+1,y)-greenAt(x+1,y))*0.5f;
                        float db=(lin(x,y-1)-greenAt(x,y-1)+lin(x,y+1)-greenAt(x,y+1))*0.5f;
                        r=g+dr; b=g+db;
                    }else{
                        g=greenAtRB(x,y); r=lin(x,y);
                        float db=(lin(x-1,y-1)-greenAt(x-1,y-1)+lin(x+1,y-1)-greenAt(x+1,y-1)+lin(x-1,y+1)-greenAt(x-1,y+1)+lin(x+1,y+1)-greenAt(x+1,y+1))*0.25f;
                        b=g+db;
                    }
                }else{
                    if(xe){
                        g=greenAtRB(x,y); b=lin(x,y);
                        float dr=(lin(x-1,y-1)-greenAt(x-1,y-1)+lin(x+1,y-1)-greenAt(x+1,y-1)+lin(x-1,y+1)-greenAt(x-1,y+1)+lin(x+1,y+1)-greenAt(x+1,y+1))*0.25f;
                        r=g+dr;
                    }else{
                        g=lin(x,y);
                        float dr=(lin(x,y-1)-greenAt(x,y-1)+lin(x,y+1)-greenAt(x,y+1))*0.5f;
                        float db=(lin(x-1,y)-greenAt(x-1,y)+lin(x+1,y)-greenAt(x+1,y))*0.5f;
                        r=g+dr; b=g+db;
                    }
                }
                break;
        }
    };

    const float* ccm = p.ccm.data();

    auto processRow = [&](int y){
        for(int x=0;x<p.width;++x){
            float r=0,g=0,b=0;
            rcdPixel(x,y,r,g,b);
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
