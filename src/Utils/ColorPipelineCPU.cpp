#include "Utils/ColorPipelineCPU.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <thread>
#include <array>

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
    auto interpG = [&](int x, int y)->float{ return 0.25f*(lin(x+1,y)+lin(x-1,y)+lin(x,y+1)+lin(x,y-1));};
    auto interpH = [&](int x, int y)->float{ return 0.5f*(lin(x+1,y)+lin(x-1,y));};
    auto interpV = [&](int x, int y)->float{ return 0.5f*(lin(x,y+1)+lin(x,y-1));};
    auto interpD = [&](int x, int y)->float{ return 0.25f*(lin(x+1,y+1)+lin(x-1,y+1)+lin(x+1,y-1)+lin(x-1,y-1));};

    struct Gradients { int n=0,ne=0,e=0,se=0,s=0,sw=0,w=0,nw=0; };

    auto readRaw = [&](int xx,int yy)->int{ return (int)readU16(raw,xx,yy,p.width,p.height); };

    auto setup5x5 = [&](int cx,int cy,std::array<int,25>& pix){
        int i=0; for(int yy=-2; yy<=2; ++yy) for(int xx=-2; xx<=2; ++xx) pix[i++] = readRaw(cx+xx, cy+yy); };

    auto getGradientsForNonGreen = [&](int x, int y, Gradients& g){
        std::array<int,25> p; setup5x5(x,y,p);
        int pix1=p[0], pix2=p[1], pix3=p[2], pix4=p[3], pix5=p[4];
        int pix6=p[5], pix7=p[6], pix8=p[7], pix9=p[8], pix10=p[9];
        int pix11=p[10], pix12=p[11], pix13=p[12], pix14=p[13], pix15=p[14];
        int pix16=p[15], pix17=p[16], pix18=p[17], pix19=p[18], pix20=p[19];
        int pix21=p[20], pix22=p[21], pix23=p[22], pix24=p[23], pix25=p[24];
        g.n  = std::abs(pix8 - pix18) + std::abs(pix3 - pix13) +
               (std::abs(pix7 - pix17)>>1) + (std::abs(pix9 - pix19)>>1) +
               (std::abs(pix2 - pix12)>>1) + (std::abs(pix4 - pix14)>>1);
        g.ne = std::abs(pix9 - pix17) + std::abs(pix5 - pix13) +
               (std::abs(pix8 - pix12)>>1) + (std::abs(pix14 - pix18)>>1) +
               (std::abs(pix4 - pix8)>>1) + (std::abs(pix10 - pix14)>>1);
        g.e  = std::abs(pix14 - pix12) + std::abs(pix15 - pix13) +
               (std::abs(pix9 - pix7)>>1) + (std::abs(pix19 - pix17)>>1) +
               (std::abs(pix10 - pix8)>>1) + (std::abs(pix20 - pix18)>>1);
        g.se = std::abs(pix19 - pix7) + std::abs(pix25 - pix13) +
               (std::abs(pix14 - pix8)>>1) + (std::abs(pix18 - pix12)>>1) +
               (std::abs(pix20 - pix14)>>1) + (std::abs(pix24 - pix18)>>1);
        g.s  = std::abs(pix18 - pix8) + std::abs(pix23 - pix13) +
               (std::abs(pix19 - pix9)>>1) + (std::abs(pix17 - pix7)>>1) +
               (std::abs(pix24 - pix14)>>1) + (std::abs(pix22 - pix12)>>1);
        g.sw = std::abs(pix17 - pix9) + std::abs(pix21 - pix13) +
               (std::abs(pix18 - pix14)>>1) + (std::abs(pix12 - pix8)>>1) +
               (std::abs(pix22 - pix18)>>1) + (std::abs(pix16 - pix12)>>1);
        g.w  = std::abs(pix12 - pix14) + std::abs(pix11 - pix13) +
               (std::abs(pix17 - pix19)>>1) + (std::abs(pix7 - pix9)>>1) +
               (std::abs(pix16 - pix18)>>1) + (std::abs(pix6 - pix8)>>1);
        g.nw = std::abs(pix7 - pix19) + std::abs(pix1 - pix13) +
               (std::abs(pix12 - pix18)>>1) + (std::abs(pix8 - pix14)>>1) +
               (std::abs(pix6 - pix12)>>1) + (std::abs(pix2 - pix8)>>1);
    };

    auto getGradientsForGreen = [&](int x, int y, Gradients& g){
        std::array<int,25> p; setup5x5(x,y,p);
        int pix1=p[0], pix2=p[1], pix3=p[2], pix4=p[3], pix5=p[4];
        int pix6=p[5], pix7=p[6], pix8=p[7], pix9=p[8], pix10=p[9];
        int pix11=p[10], pix12=p[11], pix13=p[12], pix14=p[13], pix15=p[14];
        int pix16=p[15], pix17=p[16], pix18=p[17], pix19=p[18], pix20=p[19];
        int pix21=p[20], pix22=p[21], pix23=p[22], pix24=p[23], pix25=p[24];
        g.n  = std::abs(pix3 - pix13) + std::abs(pix8 - pix18) +
               (std::abs(pix7 - pix17)>>1) + (std::abs(pix9 - pix19)>>1) +
               (std::abs(pix2 - pix12)>>1) + (std::abs(pix4 - pix14)>>1);
        g.ne = std::abs(pix9 - pix17) + std::abs(pix5 - pix13) +
               std::abs(pix4 - pix12)  + std::abs(pix10 - pix18);
        g.e  = std::abs(pix14 - pix12) + std::abs(pix15 - pix13) +
               (std::abs(pix9 - pix7)>>1) + (std::abs(pix19 - pix17)>>1) +
               (std::abs(pix10 - pix8)>>1) + (std::abs(pix20 - pix18)>>1);
        g.se = std::abs(pix19 - pix7) + std::abs(pix25 - pix13) +
               std::abs(pix20 - pix8) + std::abs(pix24 - pix12);
        g.s  = std::abs(pix18 - pix8) + std::abs(pix23 - pix13) +
               (std::abs(pix19 - pix9)>>1) + (std::abs(pix17 - pix7)>>1) +
               (std::abs(pix24 - pix14)>>1) + (std::abs(pix22 - pix12)>>1);
        g.sw = std::abs(pix17 - pix9) + std::abs(pix21 - pix13) +
               std::abs(pix22 - pix14) + std::abs(pix16 - pix8);
        g.w  = std::abs(pix12 - pix14) + std::abs(pix11 - pix13) +
               (std::abs(pix17 - pix19)>>1) + (std::abs(pix7 - pix9)>>1) +
               (std::abs(pix16 - pix18)>>1) + (std::abs(pix6 - pix8)>>1);
        g.nw = std::abs(pix7 - pix19) + std::abs(pix1 - pix13) +
               std::abs(pix6 - pix18) + std::abs(pix2 - pix14);
    };

    auto getMin = [&](const Gradients& g){ return std::min({g.n,g.ne,g.e,g.se,g.s,g.sw,g.w,g.nw}); };
    auto getMax = [&](const Gradients& g){ return std::max({g.n,g.ne,g.e,g.se,g.s,g.sw,g.w,g.nw}); };

    auto bilinearRGB = [&](int x,int y,float& r,float& g,float& b){
        bool ye = (y%2)==0; bool xe = (x%2)==0;
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
    };

    auto processGradients = [&](int x,int y,bool useGreen,float& rSum,float& gSum,float& bSum,int& numRegions, float& rOut, float& gOut, float& bOut)->bool{
        Gradients gr; if(useGreen) getGradientsForGreen(x,y,gr); else getGradientsForNonGreen(x,y,gr);
        int mn = getMin(gr); int mx = getMax(gr);
        double threshold = 1.5*mn + 0.5*(mx - mn);
        rSum=gSum=bSum=0.0f; numRegions=0;
        auto addRegion=[&](int dx,int dy){ float rr,gg,bb; bilinearRGB(x+dx,y+dy,rr,gg,bb); rSum+=rr; gSum+=gg; bSum+=bb; numRegions++; };
        if(gr.n < threshold)  addRegion(0,-1);
        if(gr.ne < threshold) addRegion(1,-1);
        if(gr.e < threshold)  addRegion(1,0);
        if(gr.se < threshold) addRegion(1,1);
        if(gr.s < threshold)  addRegion(0,1);
        if(gr.sw < threshold) addRegion(-1,1);
        if(gr.w < threshold)  addRegion(-1,0);
        if(gr.nw < threshold) addRegion(-1,-1);
        if(numRegions==0){ bilinearRGB(x,y,rOut,gOut,bOut); return false; }
        return true;
    };

    auto demosaicVG = [&](int x,int y,float& r,float& g,float& b){
        bool ye = (y%2)==0; bool xe = (x%2)==0;
        float rSum=0,gSum=0,bSum=0; int regions=0;
        if(p.cfaType==0){ // BGGR
            if(ye){
                if(xe){ // B pixel
                    b=lin(x,y); float rO,gO,bO; if(processGradients(x,y,false,rSum,gSum,bSum,regions,rO,gO,bO)){
                            r = std::clamp(b + (rSum - bSum)/regions,0.0f,1.0f);
                            g = std::clamp(b + (gSum - bSum)/regions,0.0f,1.0f);
                        } else { r=rO; g=gO; b=bO; }
                } else { // G pixel
                    g=lin(x,y); float rO,gO,bO; if(processGradients(x,y,true,rSum,gSum,bSum,regions,rO,gO,bO)){
                            r = std::clamp(g + (rSum - gSum)/regions,0.0f,1.0f);
                            b = std::clamp(g + (bSum - gSum)/regions,0.0f,1.0f);
                        } else { r=rO; g=gO; b=bO; }
                }
            } else {
                if(xe){ // G pixel
                    g=lin(x,y); float rO,gO,bO; if(processGradients(x,y,true,rSum,gSum,bSum,regions,rO,gO,bO)){
                            r = std::clamp(g + (rSum - gSum)/regions,0.0f,1.0f);
                            b = std::clamp(g + (bSum - gSum)/regions,0.0f,1.0f);
                        } else { r=rO; g=gO; b=bO; }
                } else { // R pixel
                    r=lin(x,y); float rO,gO,bO; if(processGradients(x,y,false,rSum,gSum,bSum,regions,rO,gO,bO)){
                            g = std::clamp(r + (gSum - rSum)/regions,0.0f,1.0f);
                            b = std::clamp(r + (bSum - rSum)/regions,0.0f,1.0f);
                        } else { r=rO; g=gO; b=bO; }
                }
            }
        } else if(p.cfaType==1){ // RGGB
            if(ye){
                if(xe){ // R pixel
                    r=lin(x,y); float rO,gO,bO; if(processGradients(x,y,false,rSum,gSum,bSum,regions,rO,gO,bO)){
                            g = std::clamp(r + (gSum - rSum)/regions,0.0f,1.0f);
                            b = std::clamp(r + (bSum - rSum)/regions,0.0f,1.0f);
                        } else { r=rO; g=gO; b=bO; }
                } else { // G pixel
                    g=lin(x,y); float rO,gO,bO; if(processGradients(x,y,true,rSum,gSum,bSum,regions,rO,gO,bO)){
                            r = std::clamp(g + (rSum - gSum)/regions,0.0f,1.0f);
                            b = std::clamp(g + (bSum - gSum)/regions,0.0f,1.0f);
                        } else { r=rO; g=gO; b=bO; }
                }
            } else {
                if(xe){ // G pixel
                    g=lin(x,y); float rO,gO,bO; if(processGradients(x,y,true,rSum,gSum,bSum,regions,rO,gO,bO)){
                            r = std::clamp(g + (rSum - gSum)/regions,0.0f,1.0f);
                            b = std::clamp(g + (bSum - gSum)/regions,0.0f,1.0f);
                        } else { r=rO; g=gO; b=bO; }
                } else { // B pixel
                    b=lin(x,y); float rO,gO,bO; if(processGradients(x,y,false,rSum,gSum,bSum,regions,rO,gO,bO)){
                            r = std::clamp(b + (rSum - bSum)/regions,0.0f,1.0f);
                            g = std::clamp(b + (gSum - bSum)/regions,0.0f,1.0f);
                        } else { r=rO; g=gO; b=bO; }
                }
            }
        } else if(p.cfaType==2){ // GBRG
            if(ye){
                if(xe){ // G pixel
                    g=lin(x,y); float rO,gO,bO; if(processGradients(x,y,true,rSum,gSum,bSum,regions,rO,gO,bO)){
                            r = std::clamp(g + (rSum - gSum)/regions,0.0f,1.0f);
                            b = std::clamp(g + (bSum - gSum)/regions,0.0f,1.0f);
                        } else { r=rO; g=gO; b=bO; }
                } else { // B pixel
                    b=lin(x,y); float rO,gO,bO; if(processGradients(x,y,false,rSum,gSum,bSum,regions,rO,gO,bO)){
                            r = std::clamp(b + (rSum - bSum)/regions,0.0f,1.0f);
                            g = std::clamp(b + (gSum - bSum)/regions,0.0f,1.0f);
                        } else { r=rO; g=gO; b=bO; }
                }
            } else {
                if(xe){ // R pixel
                    r=lin(x,y); float rO,gO,bO; if(processGradients(x,y,false,rSum,gSum,bSum,regions,rO,gO,bO)){
                            g = std::clamp(r + (gSum - rSum)/regions,0.0f,1.0f);
                            b = std::clamp(r + (bSum - rSum)/regions,0.0f,1.0f);
                        } else { r=rO; g=gO; b=bO; }
                } else { // G pixel
                    g=lin(x,y); float rO,gO,bO; if(processGradients(x,y,true,rSum,gSum,bSum,regions,rO,gO,bO)){
                            r = std::clamp(g + (rSum - gSum)/regions,0.0f,1.0f);
                            b = std::clamp(g + (bSum - gSum)/regions,0.0f,1.0f);
                        } else { r=rO; g=gO; b=bO; }
                }
            }
        } else { // GRBG
            if(ye){
                if(xe){ // G pixel
                    g=lin(x,y); float rO,gO,bO; if(processGradients(x,y,true,rSum,gSum,bSum,regions,rO,gO,bO)){
                            r = std::clamp(g + (rSum - gSum)/regions,0.0f,1.0f);
                            b = std::clamp(g + (bSum - gSum)/regions,0.0f,1.0f);
                        } else { r=rO; g=gO; b=bO; }
                } else { // R pixel
                    r=lin(x,y); float rO,gO,bO; if(processGradients(x,y,false,rSum,gSum,bSum,regions,rO,gO,bO)){
                            g = std::clamp(r + (gSum - rSum)/regions,0.0f,1.0f);
                            b = std::clamp(r + (bSum - rSum)/regions,0.0f,1.0f);
                        } else { r=rO; g=gO; b=bO; }
                }
            } else {
                if(xe){ // B pixel
                    b=lin(x,y); float rO,gO,bO; if(processGradients(x,y,false,rSum,gSum,bSum,regions,rO,gO,bO)){
                            r = std::clamp(b + (rSum - bSum)/regions,0.0f,1.0f);
                            g = std::clamp(b + (gSum - bSum)/regions,0.0f,1.0f);
                        } else { r=rO; g=gO; b=bO; }
                } else { // G pixel
                    g=lin(x,y); float rO,gO,bO; if(processGradients(x,y,true,rSum,gSum,bSum,regions,rO,gO,bO)){
                            r = std::clamp(g + (rSum - gSum)/regions,0.0f,1.0f);
                            b = std::clamp(g + (bSum - gSum)/regions,0.0f,1.0f);
                        } else { r=rO; g=gO; b=bO; }
                }
            }
        }
    };

    auto vh_calc=[&](int x,int y){
        float sx=0.0f, sy=0.0f;
        for(int i=-1;i<=1;++i){
            sx += lin(x+i,y-3) - 3.0f*lin(x+i,y-2) - lin(x+i,y-1) + 6.0f*lin(x+i,y) - lin(x+i,y+1) - 3.0f*lin(x+i,y+2) + lin(x+i,y+3);
            sy += lin(x-3,y+i) - 3.0f*lin(x-2,y+i) - lin(x-1,y+i) + 6.0f*lin(x,y+i) - lin(x+1,y+i) - 3.0f*lin(x+2,y+i) + lin(x+3,y+i);
        }
        float sx2=sx*sx, sy2=sy*sy;
        return (sx2)/(1e-5f + sx2 + sy2);
    };

    auto pq_calc=[&](int x,int y){
        float a=1e-5f,bv=1e-5f;
        for(int i=-1;i<=1;++i){
            a += lin(x-3+i,y-3+i) - lin(x+1+i,y-1+i) - lin(x+1+i,y+1+i) + lin(x+3+i,y+3+i)
                -3.0f*(lin(x-2+i,y-2+i)+lin(x+2+i,y+2+i)) + 6.0f*lin(x+i,y+i);
            bv += lin(x+3+i,y-3-i) - lin(x+1+i,y-1-i) - lin(x-1+i,y+1-i) + lin(x-3+i,y+3-i)
                -3.0f*(lin(x+2+i,y-2-i)+lin(x-2+i,y+2-i)) + 6.0f*lin(x+i,y-i);
        }
        a*=a; bv*=bv; return a/(a+bv);
    };

    auto lp_calc=[&](int x,int y){
        float lp=0.0f; int off=((x & 1)==1)?-1:1; const float w[3]={0.5f,1.0f,0.5f};
        for(int j=-1;j<=1;++j) for(int i=-1;i<=1;++i) lp+=w[j+1]*w[i+1]*lin(x+i+off,y+j);
        return std::max(1e-6f, lp);
    };

    auto demosaicRCD=[&](int x,int y,float& r,float& g,float& b){
        bool is_green=((x+y)&1)==1; bool is_red=!is_green && ((y&1)==0); const float eps=1e-5f;
        if(is_green){
            g=lin(x,y);
            float vh_val=vh_calc(x,y);
            float vh_neighbor=0.25f*(vh_calc(x-1,y-1)+vh_calc(x+1,y-1)+vh_calc(x-1,y+1)+vh_calc(x+1,y+1));
            float vh_discr=std::abs(0.5f - vh_val) < std::abs(0.5f - vh_neighbor) ? vh_val : vh_neighbor;
            float N_grad=eps+std::abs(lin(x,y-1)-lin(x,y+1))+std::abs(lin(x,y)-lin(x,y-2));
            float S_grad=eps+std::abs(lin(x,y-1)-lin(x,y+1))+std::abs(lin(x,y)-lin(x,y+2));
            float W_grad=eps+std::abs(lin(x-1,y)-lin(x+1,y))+std::abs(lin(x,y)-lin(x-2,y));
            float E_grad=eps+std::abs(lin(x-1,y)-lin(x+1,y))+std::abs(lin(x,y)-lin(x+2,y));
            float lp=lp_calc(x,y);
            float r_N=lin(x,y-1)*2.0f*lp/(eps+lp+lp_calc(x,y-2));
            float r_S=lin(x,y+1)*2.0f*lp/(eps+lp+lp_calc(x,y+2));
            float r_W=lin(x-1,y)*2.0f*lp/(eps+lp+lp_calc(x-2,y));
            float r_E=lin(x+1,y)*2.0f*lp/(eps+lp+lp_calc(x+2,y));
            float r_v=(S_grad*r_N+N_grad*r_S)/(N_grad+S_grad);
            float r_h=(E_grad*r_W+W_grad*r_E)/(E_grad+W_grad);
            r=mix(r_v,r_h,vh_discr);
            float b_N=lin(x,y-1)*2.0f*lp/(eps+lp+lp_calc(x,y-2));
            float b_S=lin(x,y+1)*2.0f*lp/(eps+lp+lp_calc(x,y+2));
            float b_W=lin(x-1,y)*2.0f*lp/(eps+lp+lp_calc(x-2,y));
            float b_E=lin(x+1,y)*2.0f*lp/(eps+lp+lp_calc(x+2,y));
            float b_v=(S_grad*b_N+N_grad*b_S)/(N_grad+S_grad);
            float b_h=(E_grad*b_W+W_grad*b_E)/(E_grad+W_grad);
            b=mix(b_v,b_h,vh_discr);
        }else{
            float c=lin(x,y);
            float vh_val=vh_calc(x,y);
            float vh_neighbor=0.25f*(vh_calc(x-1,y-1)+vh_calc(x+1,y-1)+vh_calc(x-1,y+1)+vh_calc(x+1,y+1));
            float vh_discr=std::abs(0.5f - vh_val) < std::abs(0.5f - vh_neighbor) ? vh_val : vh_neighbor;
            float N=lin(x,y-1), S=lin(x,y+1), W=lin(x-1,y), E=lin(x+1,y);
            float N_grad=eps+std::abs(N-lin(x,y+1))+std::abs(lin(x,y-1)-lin(x,y-3));
            float S_grad=eps+std::abs(S-lin(x,y-1))+std::abs(lin(x,y+1)-lin(x,y+3));
            float W_grad=eps+std::abs(W-lin(x+1,y))+std::abs(lin(x-1,y)-lin(x-3,y));
            float E_grad=eps+std::abs(E-lin(x-1,y))+std::abs(lin(x+1,y)-lin(x+3,y));
            float lp=lp_calc(x,y);
            float g_N=N*2.0f*lp/(eps+lp+lp_calc(x,y-2));
            float g_S=S*2.0f*lp/(eps+lp+lp_calc(x,y+2));
            float g_W=W*2.0f*lp/(eps+lp+lp_calc(x-2,y));
            float g_E=E*2.0f*lp/(eps+lp+lp_calc(x+2,y));
            float g_v=(S_grad*g_N+N_grad*g_S)/(N_grad+S_grad);
            float g_h=(E_grad*g_W+W_grad*g_E)/(E_grad+W_grad);
            g=mix(g_v,g_h,vh_discr);
            float pq_val=pq_calc(x,y);
            float pq_neighbor=0.25f*(pq_calc(x-1,y-1)+pq_calc(x+1,y-1)+pq_calc(x-1,y+1)+pq_calc(x+1,y+1));
            float pq_discr=std::abs(0.5f-pq_val)<std::abs(0.5f-pq_neighbor)?pq_val:pq_neighbor;
            float NW=lin(x-1,y-1)-(lin(x-1,y)+lin(x,y-1))*0.5f+g;
            float NE=lin(x+1,y-1)-(lin(x+1,y)+lin(x,y-1))*0.5f+g;
            float SW=lin(x-1,y+1)-(lin(x-1,y)+lin(x,y+1))*0.5f+g;
            float SE=lin(x+1,y+1)-(lin(x+1,y)+lin(x,y+1))*0.5f+g;
            float p=(NW+SE)*0.5f; float q=(NE+SW)*0.5f; float other=mix(p,q,pq_discr);
            if(is_red){ r=c; b=other; } else { b=c; r=other; }
        }
    };

    const float* ccm = p.ccm.data();

    auto processRow = [&](int y){
        for(int x=0;x<p.width;++x){
            float r=0.0f,g=0.0f,b=0.0f;
            if(p.demosaicMode==1) demosaicVG(x,y,r,g,b);
            else if(p.demosaicMode==2) demosaicRCD(x,y,r,g,b);
            else bilinearRGB(x,y,r,g,b);
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
