#include <motioncam/Decoder.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cstdint>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace fs = std::filesystem;

struct RGB {
    uint8_t r, g, b;
};

static RGB bilinearPixel(const std::vector<uint16_t>& raw,int w,int h,int x,int y,const std::string& cfa) {
    auto clamp=[&](int v,int low,int high){ return v<low?low:(v>high?high:v); };
    auto get=[&](int ix,int iy){ ix=clamp(ix,0,w-1); iy=clamp(iy,0,h-1); return raw[iy*w+ix]; };
    bool rAt00=false,bAt11=false; // for RGGB order
    std::string c=cfa; std::transform(c.begin(),c.end(),c.begin(),::tolower);
    if(c=="rggb") { rAt00=true; bAt11=true; }
    else if(c=="bggr") { rAt00=false; bAt11=false; }
    else if(c=="grbg") { rAt00=false; bAt11=true; }
    else if(c=="gbrg") { rAt00=true; bAt11=false; }
    else { rAt00=true; bAt11=true; }
    bool evenX=(x%2==0), evenY=(y%2==0);
    uint16_t r,g,b; r=g=b=0;
    if(evenY&&evenX){
        // top-left
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

int main(int argc,char** argv){
    if(argc<2){ std::cout<<"Usage: mcraw_thumbnail <file.mcraw> [output.png]\n"; return 0; }
    std::string input=argv[1];
    std::string out= (argc>2)? argv[2]: fs::path(input).replace_extension(".png").string();
    try{
        motioncam::Decoder dec(input);
        const auto& frames=dec.getFrames();
        if(frames.empty()){ std::cerr<<"No frames"<<std::endl; return 1; }
        std::vector<uint16_t> raw; nlohmann::json meta; dec.loadFrame(frames[0],raw,meta);
        auto width = meta["width"].get<int>();
        auto height = meta["height"].get<int>();
        std::string cfa = meta.value("cfaPattern", meta.value("cfa", "rggb"));
        std::vector<uint8_t> rgb(width*height*3);
        for(int y=0;y<height;++y){
            for(int x=0;x<width;++x){
                RGB px = bilinearPixel(raw,width,height,x,y,cfa);
                size_t idx=(y*width+x)*3; rgb[idx]=px.r; rgb[idx+1]=px.g; rgb[idx+2]=px.b; }
        }
        if(!stbi_write_png(out.c_str(), width,height,3, rgb.data(), width*3)){
            std::cerr<<"Failed writing "<<out<<"\n"; return 1; }
        std::cout<<"Wrote thumbnail: "<<out<<"\n"; return 0;
    }catch(const std::exception& e){ std::cerr<<"Error: "<<e.what()<<"\n"; return 1; }
}
