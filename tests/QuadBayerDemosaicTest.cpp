#include "Utils/ColorPipelineCPU.h"
#include "Utils/QuadBayerDemosaic.h"
#include <vector>
#include <iostream>

int main(){
    const int W=4,H=4; std::vector<uint16_t> raw(W*H,100);
    CPUColorParams p{}; p.width=W; p.height=H; p.cfaType=1; p.blackLevel=0.0; p.whiteLevel=65535.0;
    std::vector<uint8_t> rgb; quadBayerDemosaic(raw.data(),p,rgb,1);
    std::cout << "pixels=" << rgb.size()/3 << "\n";
    return 0;
}
