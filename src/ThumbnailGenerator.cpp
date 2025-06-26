#include "ThumbnailGenerator.h"
#include <motioncam/Decoder.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <iostream>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../tools/stb_image_write.h"

namespace fs = std::filesystem;

struct RGB { uint8_t r, g, b; };

static RGB bilinearPixel(const std::vector<uint16_t>& raw, int w, int h, int x, int y, const std::string& cfa)
{
    auto clamp = [&](int v, int low, int high){ return v<low?low:(v>high?high:v); };
    auto get = [&](int ix,int iy){ ix=clamp(ix,0,w-1); iy=clamp(iy,0,h-1); return raw[iy*w+ix]; };
    bool rAt00=false,bAt11=false; std::string c=cfa; std::transform(c.begin(),c.end(),c.begin(),::tolower);
    if(c=="rggb") { rAt00=true; bAt11=true; }
    else if(c=="bggr") { rAt00=false; bAt11=false; }
    else if(c=="grbg") { rAt00=false; bAt11=true; }
    else if(c=="gbrg") { rAt00=true; bAt11=false; }
    else { rAt00=true; bAt11=true; }
    bool evenX=(x%2==0), evenY=(y%2==0); uint16_t r=0,g=0,b=0;
    if(evenY&&evenX){
        if(rAt00){ r=get(x,y); g=(get(x+1,y)+get(x,y+1))/2; b=get(x+1,y+1); }
        else if(bAt11){ b=get(x,y); g=(get(x+1,y)+get(x,y+1))/2; r=get(x+1,y+1); }
        else { g=get(x,y); r=(get(x-1,y)+get(x+1,y))/2; b=(get(x,y-1)+get(x,y+1))/2; }
    } else if(evenY&&!evenX){
        if(rAt00){ g=get(x,y); r=(get(x-1,y)+get(x+1,y))/2; b=(get(x,y-1)+get(x,y+1))/2; }
        else if(bAt11){ g=get(x,y); b=(get(x-1,y)+get(x+1,y))/2; r=(get(x,y-1)+get(x,y+1))/2; }
        else if(c=="grbg"){ r=get(x,y); g=(get(x-1,y)+get(x+1,y)+get(x,y+1))/3; b=get(x,y+1); }
        else { b=get(x,y); g=(get(x-1,y)+get(x+1,y)+get(x,y+1))/3; r=get(x,y+1); }
    } else if(!evenY&&evenX){
        if(rAt00){ g=get(x,y); r=(get(x,y-1)+get(x,y+1))/2; b=(get(x-1,y)+get(x+1,y))/2; }
        else if(bAt11){ g=get(x,y); b=(get(x,y-1)+get(x,y+1))/2; r=(get(x-1,y)+get(x+1,y))/2; }
        else if(c=="grbg"){ b=get(x,y); g=(get(x-1,y)+get(x+1,y)+get(x,y-1))/3; r=get(x,y-1); }
        else { r=get(x,y); g=(get(x-1,y)+get(x+1,y)+get(x,y-1))/3; b=get(x,y-1); }
    } else {
        if(rAt00){ b=get(x,y); g=(get(x-1,y)+get(x,y-1))/2; r=get(x-1,y-1); }
        else if(bAt11){ r=get(x,y); g=(get(x-1,y)+get(x,y-1))/2; b=get(x-1,y-1); }
        else { g=get(x,y); r=(get(x-1,y)+get(x,y-1))/2; b=get(x-1,y-1); }
    }
    auto to8=[&](uint16_t v){ return static_cast<uint8_t>(std::clamp<int>(v>>8,0,255)); };
    return {to8(r),to8(g),to8(b)};
}

bool generateThumbnail(const std::string& mcrawPath, const std::string& outPng)
{
    try {
        motioncam::Decoder dec(mcrawPath);
        const auto& frames = dec.getFrames();
        if(frames.empty()) return false;
        std::vector<uint16_t> raw; nlohmann::json meta; dec.loadFrame(frames[0], raw, meta);
        int width = meta["width"].get<int>();
        int height = meta["height"].get<int>();
        std::string cfa = meta.value("cfaPattern", meta.value("cfa", "rggb"));
        std::vector<uint8_t> rgb(width*height*3);
        for(int y=0;y<height;++y) for(int x=0;x<width;++x){
            RGB px = bilinearPixel(raw,width,height,x,y,cfa);
            size_t idx=(y*width+x)*3; rgb[idx]=px.r; rgb[idx+1]=px.g; rgb[idx+2]=px.b;
        }
        if(!stbi_write_png(outPng.c_str(), width, height, 3, rgb.data(), width*3))
            return false;
        return true;
    } catch(const std::exception&){
        return false;
    }
}

