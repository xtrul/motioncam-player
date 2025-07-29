#include "Utils/QuadBayerDemosaic.h"
#include <algorithm>
#include <cmath>

static inline float readNorm(const uint16_t* r,int x,int y,int w,int h){
    x = std::clamp(x,0,w-1);
    y = std::clamp(y,0,h-1);
    return r[y*w+x]/65535.f;
}

static inline float greenEdge(const uint16_t* r,int x,int y,int w,int h){
    float gh = std::abs(readNorm(r,x-1,y,w,h)-readNorm(r,x+1,y,w,h)) +
               std::abs(readNorm(r,x-2,y,w,h)-readNorm(r,x+2,y,w,h));
    float gv = std::abs(readNorm(r,x,y-1,w,h)-readNorm(r,x,y+1,w,h)) +
               std::abs(readNorm(r,x,y-2,w,h)-readNorm(r,x,y+2,w,h));
    if(gh < gv) return (readNorm(r,x-1,y,w,h)+readNorm(r,x+1,y,w,h))*0.5f;
    if(gv < gh) return (readNorm(r,x,y-1,w,h)+readNorm(r,x,y+1,w,h))*0.5f;
    return (readNorm(r,x-1,y,w,h)+readNorm(r,x+1,y,w,h)+
            readNorm(r,x,y-1,w,h)+readNorm(r,x,y+1,w,h))*0.25f;
}

void quadBayerDemosaic(const uint16_t* raw,const QuadBayerDemosaicParams& p,
                       std::vector<float>& outRGB)
{
    int w=p.width,h=p.height; outRGB.resize(w*h*3);
    for(int y=0;y<h;++y){
        bool ye=(y&1)==0;
        for(int x=0;x<w;++x){
            bool xe=(x&1)==0;
            float r=0,g=0,b=0;
            switch(p.cfaType){
                case 0: // BGGR
                    if(ye){
                        if(xe){ b=readNorm(raw,x,y,w,h); g=greenEdge(raw,x,y,w,h); r=(readNorm(raw,x-1,y-1,w,h)+readNorm(raw,x+1,y-1,w,h)+readNorm(raw,x-1,y+1,w,h)+readNorm(raw,x+1,y+1,w,h))*0.25f; }
                        else { g=readNorm(raw,x,y,w,h); r=(readNorm(raw,x,y-1,w,h)+readNorm(raw,x,y+1,w,h))*0.5f; b=(readNorm(raw,x-1,y,w,h)+readNorm(raw,x+1,y,w,h))*0.5f; }
                    }else{
                        if(xe){ g=readNorm(raw,x,y,w,h); r=(readNorm(raw,x-1,y,w,h)+readNorm(raw,x+1,y,w,h))*0.5f; b=(readNorm(raw,x,y-1,w,h)+readNorm(raw,x,y+1,w,h))*0.5f; }
                        else { r=readNorm(raw,x,y,w,h); g=greenEdge(raw,x,y,w,h); b=(readNorm(raw,x-1,y-1,w,h)+readNorm(raw,x+1,y-1,w,h)+readNorm(raw,x-1,y+1,w,h)+readNorm(raw,x+1,y+1,w,h))*0.25f; }
                    }
                    break;
                case 1: // RGGB
                    if(ye){
                        if(xe){ r=readNorm(raw,x,y,w,h); g=greenEdge(raw,x,y,w,h); b=(readNorm(raw,x-1,y-1,w,h)+readNorm(raw,x+1,y-1,w,h)+readNorm(raw,x-1,y+1,w,h)+readNorm(raw,x+1,y+1,w,h))*0.25f; }
                        else { g=readNorm(raw,x,y,w,h); r=(readNorm(raw,x-1,y,w,h)+readNorm(raw,x+1,y,w,h))*0.5f; b=(readNorm(raw,x,y-1,w,h)+readNorm(raw,x,y+1,w,h))*0.5f; }
                    }else{
                        if(xe){ g=readNorm(raw,x,y,w,h); r=(readNorm(raw,x,y-1,w,h)+readNorm(raw,x,y+1,w,h))*0.5f; b=(readNorm(raw,x-1,y,w,h)+readNorm(raw,x+1,y,w,h))*0.5f; }
                        else { b=readNorm(raw,x,y,w,h); g=greenEdge(raw,x,y,w,h); r=(readNorm(raw,x-1,y-1,w,h)+readNorm(raw,x+1,y-1,w,h)+readNorm(raw,x-1,y+1,w,h)+readNorm(raw,x+1,y+1,w,h))*0.25f; }
                    }
                    break;
                case 2: // GBRG
                    if(ye){
                        if(xe){ g=readNorm(raw,x,y,w,h); r=(readNorm(raw,x,y-1,w,h)+readNorm(raw,x,y+1,w,h))*0.5f; b=(readNorm(raw,x-1,y,w,h)+readNorm(raw,x+1,y,w,h))*0.5f; }
                        else { b=readNorm(raw,x,y,w,h); g=greenEdge(raw,x,y,w,h); r=(readNorm(raw,x-1,y-1,w,h)+readNorm(raw,x+1,y-1,w,h)+readNorm(raw,x-1,y+1,w,h)+readNorm(raw,x+1,y+1,w,h))*0.25f; }
                    }else{
                        if(xe){ r=readNorm(raw,x,y,w,h); g=greenEdge(raw,x,y,w,h); b=(readNorm(raw,x-1,y-1,w,h)+readNorm(raw,x+1,y-1,w,h)+readNorm(raw,x-1,y+1,w,h)+readNorm(raw,x+1,y+1,w,h))*0.25f; }
                        else { g=readNorm(raw,x,y,w,h); r=(readNorm(raw,x-1,y,w,h)+readNorm(raw,x+1,y,w,h))*0.5f; b=(readNorm(raw,x,y-1,w,h)+readNorm(raw,x,y+1,w,h))*0.5f; }
                    }
                    break;
                default: // GRBG
                    if(ye){
                        if(xe){ g=readNorm(raw,x,y,w,h); r=(readNorm(raw,x-1,y,w,h)+readNorm(raw,x+1,y,w,h))*0.5f; b=(readNorm(raw,x,y-1,w,h)+readNorm(raw,x,y+1,w,h))*0.5f; }
                        else { r=readNorm(raw,x,y,w,h); g=greenEdge(raw,x,y,w,h); b=(readNorm(raw,x-1,y-1,w,h)+readNorm(raw,x+1,y-1,w,h)+readNorm(raw,x-1,y+1,w,h)+readNorm(raw,x+1,y+1,w,h))*0.25f; }
                    }else{
                        if(xe){ b=readNorm(raw,x,y,w,h); g=greenEdge(raw,x,y,w,h); r=(readNorm(raw,x-1,y-1,w,h)+readNorm(raw,x+1,y-1,w,h)+readNorm(raw,x-1,y+1,w,h)+readNorm(raw,x+1,y+1,w,h))*0.25f; }
                        else { g=readNorm(raw,x,y,w,h); r=(readNorm(raw,x,y-1,w,h)+readNorm(raw,x,y+1,w,h))*0.5f; b=(readNorm(raw,x-1,y,w,h)+readNorm(raw,x+1,y,w,h))*0.5f; }
                    }
                    break;
            }
            outRGB[(y*w+x)*3+0]=r;
            outRGB[(y*w+x)*3+1]=g;
            outRGB[(y*w+x)*3+2]=b;
        }
    }
}
