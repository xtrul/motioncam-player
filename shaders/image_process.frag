#version 450

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform usampler2D rawImageBufferTexture;

layout(binding = 1) uniform ShaderParams {
    int W;
    int H;
    int cfaType; // 0:BGGR, 1:RGGB, 2:GBRG, 3:GRBG
    float exposure;
    float blackLevel;
    float whiteLevel;
    float invBlackWhiteRange; // Precomputed 1.0 / (whiteLevel - blackLevel)
    float gainR;
    float gainG;
    float gainB;
    mat4 CCM; // Pass as mat4, use top-left 3x3
    float saturationAdjustment; // e.g., 1.0 for no change, 1.25 for +25%
    int orientationDegrees;
} params;

// sRGB EOTF (gamma correction)
float srgb_eotf(float v) {
    v = clamp(v, 0.0, 1.0);
    return (v <= 0.0031308) ? v * 12.92 : 1.055 * pow(v, 1.0/2.4) - 0.055;
}

uint readU16_val(int x, int y) {
    x = clamp(x, 0, params.W - 1);
    y = clamp(y, 0, params.H - 1);
    return texelFetch(rawImageBufferTexture, ivec2(x, y), 0).r;
}

float lin(uint v_u16) {
    float t = (float(v_u16) - params.blackLevel) * params.invBlackWhiteRange;
    return clamp(t * params.exposure, 0.0, 1.0);
}

float interpG(int x, int y) {
    return 0.25 * (lin(readU16_val(x + 1, y)) + lin(readU16_val(x - 1, y)) +
                   lin(readU16_val(x, y + 1)) + lin(readU16_val(x, y - 1)));
}
float interpH(int x, int y) {
    return 0.5 * (lin(readU16_val(x + 1, y)) + lin(readU16_val(x - 1, y)));
}
float interpV(int x, int y) {
    return 0.5 * (lin(readU16_val(x, y + 1)) + lin(readU16_val(x, y - 1)));
}
float interpD(int x, int y) {
    return 0.25 * (lin(readU16_val(x + 1, y + 1)) + lin(readU16_val(x - 1, y + 1)) +
                   lin(readU16_val(x + 1, y - 1)) + lin(readU16_val(x - 1, y - 1)));
}

struct Gradients { int n; int ne; int e; int se; int s; int sw; int w; int nw; };

int readRaw(int x,int y){ return int(readU16_val(x,y)); }

void setup5x5(int cx,int cy,out int pix[25]){
    int idx=0; for(int yy=-2;yy<=2;yy++) for(int xx=-2;xx<=2;xx++) { pix[idx++] = readRaw(cx+xx,cy+yy); }
}

void getGradientsForNonGreen(int x,int y,out Gradients g){
    int p[25]; setup5x5(x,y,p);
    int pix1=p[0],pix2=p[1],pix3=p[2],pix4=p[3],pix5=p[4];
    int pix6=p[5],pix7=p[6],pix8=p[7],pix9=p[8],pix10=p[9];
    int pix11=p[10],pix12=p[11],pix13=p[12],pix14=p[13],pix15=p[14];
    int pix16=p[15],pix17=p[16],pix18=p[17],pix19=p[18],pix20=p[19];
    int pix21=p[20],pix22=p[21],pix23=p[22],pix24=p[23],pix25=p[24];
    g.n  = abs(pix8 - pix18) + abs(pix3 - pix13) +
           (abs(pix7 - pix17)>>1) + (abs(pix9 - pix19)>>1) +
           (abs(pix2 - pix12)>>1) + (abs(pix4 - pix14)>>1);
    g.ne = abs(pix9 - pix17) + abs(pix5 - pix13) +
           (abs(pix8 - pix12)>>1) + (abs(pix14 - pix18)>>1) +
           (abs(pix4 - pix8)>>1) + (abs(pix10 - pix14)>>1);
    g.e  = abs(pix14 - pix12) + abs(pix15 - pix13) +
           (abs(pix9 - pix7)>>1) + (abs(pix19 - pix17)>>1) +
           (abs(pix10 - pix8)>>1) + (abs(pix20 - pix18)>>1);
    g.se = abs(pix19 - pix7) + abs(pix25 - pix13) +
           (abs(pix14 - pix8)>>1) + (abs(pix18 - pix12)>>1) +
           (abs(pix20 - pix14)>>1) + (abs(pix24 - pix18)>>1);
    g.s  = abs(pix18 - pix8) + abs(pix23 - pix13) +
           (abs(pix19 - pix9)>>1) + (abs(pix17 - pix7)>>1) +
           (abs(pix24 - pix14)>>1) + (abs(pix22 - pix12)>>1);
    g.sw = abs(pix17 - pix9) + abs(pix21 - pix13) +
           (abs(pix18 - pix14)>>1) + (abs(pix12 - pix8)>>1) +
           (abs(pix22 - pix18)>>1) + (abs(pix16 - pix12)>>1);
    g.w  = abs(pix12 - pix14) + abs(pix11 - pix13) +
           (abs(pix17 - pix19)>>1) + (abs(pix7 - pix9)>>1) +
           (abs(pix16 - pix18)>>1) + (abs(pix6 - pix8)>>1);
    g.nw = abs(pix7 - pix19) + abs(pix1 - pix13) +
           (abs(pix12 - pix18)>>1) + (abs(pix8 - pix14)>>1) +
           (abs(pix6 - pix12)>>1) + (abs(pix2 - pix8)>>1);
}

void getGradientsForGreen(int x,int y,out Gradients g){
    int p[25]; setup5x5(x,y,p);
    int pix1=p[0],pix2=p[1],pix3=p[2],pix4=p[3],pix5=p[4];
    int pix6=p[5],pix7=p[6],pix8=p[7],pix9=p[8],pix10=p[9];
    int pix11=p[10],pix12=p[11],pix13=p[12],pix14=p[13],pix15=p[14];
    int pix16=p[15],pix17=p[16],pix18=p[17],pix19=p[18],pix20=p[19];
    int pix21=p[20],pix22=p[21],pix23=p[22],pix24=p[23],pix25=p[24];
    g.n  = abs(pix3 - pix13) + abs(pix8 - pix18) +
           (abs(pix7 - pix17)>>1) + (abs(pix9 - pix19)>>1) +
           (abs(pix2 - pix12)>>1) + (abs(pix4 - pix14)>>1);
    g.ne = abs(pix9 - pix17) + abs(pix5 - pix13) +
           abs(pix4 - pix12)  + abs(pix10 - pix18);
    g.e  = abs(pix14 - pix12) + abs(pix15 - pix13) +
           (abs(pix9 - pix7)>>1) + (abs(pix19 - pix17)>>1) +
           (abs(pix10 - pix8)>>1) + (abs(pix20 - pix18)>>1);
    g.se = abs(pix19 - pix7) + abs(pix25 - pix13) +
           abs(pix20 - pix8) + abs(pix24 - pix12);
    g.s  = abs(pix18 - pix8) + abs(pix23 - pix13) +
           (abs(pix19 - pix9)>>1) + (abs(pix17 - pix7)>>1) +
           (abs(pix24 - pix14)>>1) + (abs(pix22 - pix12)>>1);
    g.sw = abs(pix17 - pix9) + abs(pix21 - pix13) +
           abs(pix22 - pix14) + abs(pix16 - pix8);
    g.w  = abs(pix12 - pix14) + abs(pix11 - pix13) +
           (abs(pix17 - pix19)>>1) + (abs(pix7 - pix9)>>1) +
           (abs(pix16 - pix18)>>1) + (abs(pix6 - pix8)>>1);
    g.nw = abs(pix7 - pix19) + abs(pix1 - pix13) +
           abs(pix6 - pix18) + abs(pix2 - pix14);
}

int gradMin(Gradients g){
    return min(min(min(g.n,g.ne),min(g.e,g.se)),min(min(g.s,g.sw),min(g.w,g.nw)));
}
int gradMax(Gradients g){
    return max(max(max(g.n,g.ne),max(g.e,g.se)),max(max(g.s,g.sw),max(g.w,g.nw)));
}

vec3 bilinearRGB(int x,int y){
    bool ye = (y % 2) == 0;
    bool xe = (x % 2) == 0;
    vec3 c = vec3(0.0);
    if (params.cfaType == 0) { // BGGR
        if (ye) {
            if (xe) { c.b = lin(readU16_val(x,y)); c.g = interpG(x,y); c.r = interpD(x,y); }
            else { c.g = lin(readU16_val(x,y)); c.r = interpV(x,y); c.b = interpH(x,y); }
        } else {
            if (xe) { c.g = lin(readU16_val(x,y)); c.r = interpH(x,y); c.b = interpV(x,y); }
            else { c.r = lin(readU16_val(x,y)); c.g = interpG(x,y); c.b = interpD(x,y); }
        }
    } else if (params.cfaType == 1) { // RGGB
        if (ye) {
            if (xe) { c.r = lin(readU16_val(x,y)); c.g = interpG(x,y); c.b = interpD(x,y); }
            else { c.g = lin(readU16_val(x,y)); c.r = interpH(x,y); c.b = interpV(x,y); }
        } else {
            if (xe) { c.g = lin(readU16_val(x,y)); c.r = interpV(x,y); c.b = interpH(x,y); }
            else { c.b = lin(readU16_val(x,y)); c.g = interpG(x,y); c.r = interpD(x,y); }
        }
    } else if (params.cfaType == 2) { // GBRG
        if (ye) {
            if (xe) { c.g = lin(readU16_val(x,y)); c.r = interpV(x,y); c.b = interpH(x,y); }
            else { c.b = lin(readU16_val(x,y)); c.g = interpG(x,y); c.r = interpD(x,y); }
        } else {
            if (xe) { c.r = lin(readU16_val(x,y)); c.g = interpG(x,y); c.b = interpD(x,y); }
            else { c.g = lin(readU16_val(x,y)); c.r = interpH(x,y); c.b = interpV(x,y); }
        }
    } else { // GRBG
        if (ye) {
            if (xe) { c.g = lin(readU16_val(x,y)); c.r = interpH(x,y); c.b = interpV(x,y); }
            else { c.r = lin(readU16_val(x,y)); c.g = interpG(x,y); c.b = interpD(x,y); }
        } else {
            if (xe) { c.b = lin(readU16_val(x,y)); c.g = interpG(x,y); c.r = interpD(x,y); }
            else { c.g = lin(readU16_val(x,y)); c.r = interpV(x,y); c.b = interpH(x,y); }
        }
    }
    return c;
}

bool processGradients(int x,int y,bool useGreen,out vec3 sum,out int numRegions,out vec3 current){
    Gradients g; if(useGreen) getGradientsForGreen(x,y,g); else getGradientsForNonGreen(x,y,g);
    int mn = gradMin(g); int mx = gradMax(g);
    float threshold = 1.5*float(mn) + 0.5*(float(mx - mn));
    sum = vec3(0.0); numRegions = 0;
    if(g.n < threshold){ sum += bilinearRGB(x, y-1); numRegions++; }
    if(g.ne < threshold){ sum += bilinearRGB(x+1,y-1); numRegions++; }
    if(g.e < threshold){ sum += bilinearRGB(x+1,y); numRegions++; }
    if(g.se < threshold){ sum += bilinearRGB(x+1,y+1); numRegions++; }
    if(g.s < threshold){ sum += bilinearRGB(x,y+1); numRegions++; }
    if(g.sw < threshold){ sum += bilinearRGB(x-1,y+1); numRegions++; }
    if(g.w < threshold){ sum += bilinearRGB(x-1,y); numRegions++; }
    if(g.nw < threshold){ sum += bilinearRGB(x-1,y-1); numRegions++; }
    if(numRegions==0){ sum = current; return false; }
    return true;
}

vec3 demosaicVG(int x,int y){
    vec3 rgb = vec3(0.0);
    bool ye = (y % 2) == 0;
    bool xe = (x % 2) == 0;
    vec3 sum; int regions; vec3 cur;
    if (params.cfaType == 0) {
        if (ye) {
            if (xe) { // B
                cur = bilinearRGB(x,y); rgb.b = cur.b;
                if(processGradients(x,y,false,sum,regions,cur)){
                    rgb.r = clamp(rgb.b + (sum.r - sum.b)/regions,0.0,1.0);
                    rgb.g = clamp(rgb.b + (sum.g - sum.b)/regions,0.0,1.0);
                } else { rgb = sum; }
            } else { // G
                cur = bilinearRGB(x,y); rgb.g = cur.g;
                if(processGradients(x,y,true,sum,regions,cur)){
                    rgb.r = clamp(rgb.g + (sum.r - sum.g)/regions,0.0,1.0);
                    rgb.b = clamp(rgb.g + (sum.b - sum.g)/regions,0.0,1.0);
                } else { rgb = sum; }
            }
        } else {
            if (xe) { // G
                cur = bilinearRGB(x,y); rgb.g = cur.g;
                if(processGradients(x,y,true,sum,regions,cur)){
                    rgb.r = clamp(rgb.g + (sum.r - sum.g)/regions,0.0,1.0);
                    rgb.b = clamp(rgb.g + (sum.b - sum.g)/regions,0.0,1.0);
                } else { rgb = sum; }
            } else { // R
                cur = bilinearRGB(x,y); rgb.r = cur.r;
                if(processGradients(x,y,false,sum,regions,cur)){
                    rgb.g = clamp(rgb.r + (sum.g - sum.r)/regions,0.0,1.0);
                    rgb.b = clamp(rgb.r + (sum.b - sum.r)/regions,0.0,1.0);
                } else { rgb = sum; }
            }
        }
    } else if (params.cfaType == 1) {
        if (ye) {
            if (xe) { // R
                cur = bilinearRGB(x,y); rgb.r = cur.r;
                if(processGradients(x,y,false,sum,regions,cur)){
                    rgb.g = clamp(rgb.r + (sum.g - sum.r)/regions,0.0,1.0);
                    rgb.b = clamp(rgb.r + (sum.b - sum.r)/regions,0.0,1.0);
                } else { rgb = sum; }
            } else { // G
                cur = bilinearRGB(x,y); rgb.g = cur.g;
                if(processGradients(x,y,true,sum,regions,cur)){
                    rgb.r = clamp(rgb.g + (sum.r - sum.g)/regions,0.0,1.0);
                    rgb.b = clamp(rgb.g + (sum.b - sum.g)/regions,0.0,1.0);
                } else { rgb = sum; }
            }
        } else {
            if (xe) { // G
                cur = bilinearRGB(x,y); rgb.g = cur.g;
                if(processGradients(x,y,true,sum,regions,cur)){
                    rgb.r = clamp(rgb.g + (sum.r - sum.g)/regions,0.0,1.0);
                    rgb.b = clamp(rgb.g + (sum.b - sum.g)/regions,0.0,1.0);
                } else { rgb = sum; }
            } else { // B
                cur = bilinearRGB(x,y); rgb.b = cur.b;
                if(processGradients(x,y,false,sum,regions,cur)){
                    rgb.r = clamp(rgb.b + (sum.r - sum.b)/regions,0.0,1.0);
                    rgb.g = clamp(rgb.b + (sum.g - sum.b)/regions,0.0,1.0);
                } else { rgb = sum; }
            }
        }
    } else if (params.cfaType == 2) {
        if (ye) {
            if (xe) { // G
                cur = bilinearRGB(x,y); rgb.g = cur.g;
                if(processGradients(x,y,true,sum,regions,cur)){
                    rgb.r = clamp(rgb.g + (sum.r - sum.g)/regions,0.0,1.0);
                    rgb.b = clamp(rgb.g + (sum.b - sum.g)/regions,0.0,1.0);
                } else { rgb = sum; }
            } else { // B
                cur = bilinearRGB(x,y); rgb.b = cur.b;
                if(processGradients(x,y,false,sum,regions,cur)){
                    rgb.r = clamp(rgb.b + (sum.r - sum.b)/regions,0.0,1.0);
                    rgb.g = clamp(rgb.b + (sum.g - sum.b)/regions,0.0,1.0);
                } else { rgb = sum; }
            }
        } else {
            if (xe) { // R
                cur = bilinearRGB(x,y); rgb.r = cur.r;
                if(processGradients(x,y,false,sum,regions,cur)){
                    rgb.g = clamp(rgb.r + (sum.g - sum.r)/regions,0.0,1.0);
                    rgb.b = clamp(rgb.r + (sum.b - sum.r)/regions,0.0,1.0);
                } else { rgb = sum; }
            } else { // G
                cur = bilinearRGB(x,y); rgb.g = cur.g;
                if(processGradients(x,y,true,sum,regions,cur)){
                    rgb.r = clamp(rgb.g + (sum.r - sum.g)/regions,0.0,1.0);
                    rgb.b = clamp(rgb.g + (sum.b - sum.g)/regions,0.0,1.0);
                } else { rgb = sum; }
            }
        }
    } else {
        if (ye) {
            if (xe) { // G
                cur = bilinearRGB(x,y); rgb.g = cur.g;
                if(processGradients(x,y,true,sum,regions,cur)){
                    rgb.r = clamp(rgb.g + (sum.r - sum.g)/regions,0.0,1.0);
                    rgb.b = clamp(rgb.g + (sum.b - sum.g)/regions,0.0,1.0);
                } else { rgb = sum; }
            } else { // R
                cur = bilinearRGB(x,y); rgb.r = cur.r;
                if(processGradients(x,y,false,sum,regions,cur)){
                    rgb.g = clamp(rgb.r + (sum.g - sum.r)/regions,0.0,1.0);
                    rgb.b = clamp(rgb.r + (sum.b - sum.r)/regions,0.0,1.0);
                } else { rgb = sum; }
            }
        } else {
            if (xe) { // B
                cur = bilinearRGB(x,y); rgb.b = cur.b;
                if(processGradients(x,y,false,sum,regions,cur)){
                    rgb.r = clamp(rgb.b + (sum.r - sum.b)/regions,0.0,1.0);
                    rgb.g = clamp(rgb.b + (sum.g - sum.b)/regions,0.0,1.0);
                } else { rgb = sum; }
            } else { // G
                cur = bilinearRGB(x,y); rgb.g = cur.g;
                if(processGradients(x,y,true,sum,regions,cur)){
                    rgb.r = clamp(rgb.g + (sum.r - sum.g)/regions,0.0,1.0);
                    rgb.b = clamp(rgb.g + (sum.b - sum.g)/regions,0.0,1.0);
                } else { rgb = sum; }
            }
        }
    }
    return rgb;
}

float vh_calc(int x,int y){
    float sx = 0.0;
    float sy = 0.0;
    for(int i=-1;i<=1;++i){
        sx += lin(readU16_val(x+i,y-3)) - 3.0*lin(readU16_val(x+i,y-2)) - lin(readU16_val(x+i,y-1)) +
              6.0*lin(readU16_val(x+i,y)) - lin(readU16_val(x+i,y+1)) - 3.0*lin(readU16_val(x+i,y+2)) +
              lin(readU16_val(x+i,y+3));
        sy += lin(readU16_val(x-3,y+i)) - 3.0*lin(readU16_val(x-2,y+i)) - lin(readU16_val(x-1,y+i)) +
              6.0*lin(readU16_val(x,y+i)) - lin(readU16_val(x+1,y+i)) - 3.0*lin(readU16_val(x+2,y+i)) +
              lin(readU16_val(x+3,y+i));
    }
    float sx2=sx*sx, sy2=sy*sy;
    return sx2/(1e-5 + sx2 + sy2);
}

float pq_calc(int x,int y){
    float a=1e-5, bv=1e-5;
    for(int i=-1;i<=1;++i){
        a += lin(readU16_val(x-3+i,y-3+i)) - lin(readU16_val(x+1+i,y-1+i)) - lin(readU16_val(x+1+i,y+1+i)) +
             lin(readU16_val(x+3+i,y+3+i)) - 3.0*(lin(readU16_val(x-2+i,y-2+i)) + lin(readU16_val(x+2+i,y+2+i))) +
             6.0*lin(readU16_val(x+i,y+i));
        bv += lin(readU16_val(x+3+i,y-3-i)) - lin(readU16_val(x+1+i,y-1-i)) - lin(readU16_val(x-1+i,y+1-i)) +
              lin(readU16_val(x-3+i,y+3-i)) - 3.0*(lin(readU16_val(x+2+i,y-2-i)) + lin(readU16_val(x-2+i,y+2-i))) +
              6.0*lin(readU16_val(x+i,y-i));
    }
    a*=a; bv*=bv; return a/(a+bv);
}

float lp_calc(int x,int y){
    float lp=0.0; int off=((x & 1)==1)?-1:1; float w[3] = float[3](0.5,1.0,0.5);
    for(int j=-1;j<=1;++j) for(int i=-1;i<=1;++i)
        lp += w[j+1]*w[i+1]*lin(readU16_val(x+i+off,y+j));
    return max(1e-6, lp);
}

vec3 demosaicRCD(int x,int y){
    bool is_green=((x+y)&1)==1;
    bool is_red=!is_green && ((y&1)==0);
    const float eps=1e-5;
    vec3 rgb=vec3(0.0);
    if(is_green){
        rgb.g = lin(readU16_val(x,y));
        float vh_val=vh_calc(x,y);
        float vh_neighbor=0.25*(vh_calc(x-1,y-1)+vh_calc(x+1,y-1)+vh_calc(x-1,y+1)+vh_calc(x+1,y+1));
        float vh_discr=abs(0.5 - vh_val) < abs(0.5 - vh_neighbor) ? vh_val : vh_neighbor;
        float N_grad=eps+abs(lin(readU16_val(x,y-1))-lin(readU16_val(x,y+1)))+abs(lin(readU16_val(x,y))-lin(readU16_val(x,y-2)));
        float S_grad=eps+abs(lin(readU16_val(x,y-1))-lin(readU16_val(x,y+1)))+abs(lin(readU16_val(x,y))-lin(readU16_val(x,y+2)));
        float W_grad=eps+abs(lin(readU16_val(x-1,y))-lin(readU16_val(x+1,y)))+abs(lin(readU16_val(x,y))-lin(readU16_val(x-2,y)));
        float E_grad=eps+abs(lin(readU16_val(x-1,y))-lin(readU16_val(x+1,y)))+abs(lin(readU16_val(x,y))-lin(readU16_val(x+2,y)));
        float lp=lp_calc(x,y);
        float r_N=lin(readU16_val(x,y-1))*2.0*lp/(eps+lp+lp_calc(x,y-2));
        float r_S=lin(readU16_val(x,y+1))*2.0*lp/(eps+lp+lp_calc(x,y+2));
        float r_W=lin(readU16_val(x-1,y))*2.0*lp/(eps+lp+lp_calc(x-2,y));
        float r_E=lin(readU16_val(x+1,y))*2.0*lp/(eps+lp+lp_calc(x+2,y));
        float r_v=(S_grad*r_N+N_grad*r_S)/(N_grad+S_grad);
        float r_h=(E_grad*r_W+W_grad*r_E)/(E_grad+W_grad);
        rgb.r=mix(r_v,r_h,vh_discr);
        float b_N=lin(readU16_val(x,y-1))*2.0*lp/(eps+lp+lp_calc(x,y-2));
        float b_S=lin(readU16_val(x,y+1))*2.0*lp/(eps+lp+lp_calc(x,y+2));
        float b_W=lin(readU16_val(x-1,y))*2.0*lp/(eps+lp+lp_calc(x-2,y));
        float b_E=lin(readU16_val(x+1,y))*2.0*lp/(eps+lp+lp_calc(x+2,y));
        float b_v=(S_grad*b_N+N_grad*b_S)/(N_grad+S_grad);
        float b_h=(E_grad*b_W+W_grad*b_E)/(E_grad+W_grad);
        rgb.b=mix(b_v,b_h,vh_discr);
    } else {
        float c=lin(readU16_val(x,y));
        float vh_val=vh_calc(x,y);
        float vh_neighbor=0.25*(vh_calc(x-1,y-1)+vh_calc(x+1,y-1)+vh_calc(x-1,y+1)+vh_calc(x+1,y+1));
        float vh_discr=abs(0.5 - vh_val) < abs(0.5 - vh_neighbor) ? vh_val : vh_neighbor;
        float N=lin(readU16_val(x,y-1));
        float S=lin(readU16_val(x,y+1));
        float W=lin(readU16_val(x-1,y));
        float E=lin(readU16_val(x+1,y));
        float N_grad=eps+abs(N-lin(readU16_val(x,y+1)))+abs(lin(readU16_val(x,y-1))-lin(readU16_val(x,y-3)));
        float S_grad=eps+abs(S-lin(readU16_val(x,y-1)))+abs(lin(readU16_val(x,y+1))-lin(readU16_val(x,y+3)));
        float W_grad=eps+abs(W-lin(readU16_val(x+1,y)))+abs(lin(readU16_val(x-1,y))-lin(readU16_val(x-3,y)));
        float E_grad=eps+abs(E-lin(readU16_val(x-1,y)))+abs(lin(readU16_val(x+1,y))-lin(readU16_val(x+3,y)));
        float lp=lp_calc(x,y);
        float g_N=N*2.0*lp/(eps+lp+lp_calc(x,y-2));
        float g_S=S*2.0*lp/(eps+lp+lp_calc(x,y+2));
        float g_W=W*2.0*lp/(eps+lp+lp_calc(x-2,y));
        float g_E=E*2.0*lp/(eps+lp+lp_calc(x+2,y));
        float g_v=(S_grad*g_N+N_grad*g_S)/(N_grad+S_grad);
        float g_h=(E_grad*g_W+W_grad*g_E)/(E_grad+W_grad);
        rgb.g=mix(g_v,g_h,vh_discr);
        float pq_val=pq_calc(x,y);
        float pq_neighbor=0.25*(pq_calc(x-1,y-1)+pq_calc(x+1,y-1)+pq_calc(x-1,y+1)+pq_calc(x+1,y+1));
        float pq_discr=abs(0.5-pq_val)<abs(0.5-pq_neighbor)?pq_val:pq_neighbor;
        float NW=lin(readU16_val(x-1,y-1))-(lin(readU16_val(x-1,y))+lin(readU16_val(x,y-1)))*0.5+rgb.g;
        float NE=lin(readU16_val(x+1,y-1))-(lin(readU16_val(x+1,y))+lin(readU16_val(x,y-1)))*0.5+rgb.g;
        float SW=lin(readU16_val(x-1,y+1))-(lin(readU16_val(x-1,y))+lin(readU16_val(x,y+1)))*0.5+rgb.g;
        float SE=lin(readU16_val(x+1,y+1))-(lin(readU16_val(x+1,y))+lin(readU16_val(x,y+1)))*0.5+rgb.g;
        float p=(NW+SE)*0.5;
        float q=(NE+SW)*0.5;
        float other=mix(p,q,pq_discr);
        if(is_red){ rgb.r=c; rgb.b=other; } else { rgb.b=c; rgb.r=other; }
    }
    return rgb;
}

void main() {
    ivec2 p = ivec2(inTexCoord * vec2(params.W, params.H));
    
    if (p.x >= params.W || p.y >= params.H || p.x < 0 || p.y < 0) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0); 
        return;
    }

    int x = p.x;
    int y = p.y;

    vec3 rgb = demosaicRCD(x,y);
    float r_demosaiced = rgb.r;
    float g_demosaiced = rgb.g;
    float b_demosaiced = rgb.b;

    float r_wb = clamp(r_demosaiced * params.gainR, 0.0, 1.0);
    float g_wb = clamp(g_demosaiced * params.gainG, 0.0, 1.0);
    float b_wb = clamp(b_demosaiced * params.gainB, 0.0, 1.0);

    mat3 ccm3x3 = mat3(params.CCM[0].xyz, params.CCM[1].xyz, params.CCM[2].xyz);
    vec3 col_linear_corrected = ccm3x3 * vec3(r_wb, g_wb, b_wb);
    col_linear_corrected = clamp(col_linear_corrected, 0.0, 1.0);

    // --- START SATURATION ADJUSTMENT ---
    // Calculate luminance (grayscale value)
    // Standard Rec.709 luma coefficients
    float luminance = dot(col_linear_corrected, vec3(0.2126, 0.7152, 0.0722)); 
    vec3 grayscale = vec3(luminance);
    
    // Interpolate between grayscale and original color based on saturationAdjustment
    // params.saturationAdjustment = 1.0 for no change
    // params.saturationAdjustment = 1.25 for +25% saturation
    // params.saturationAdjustment = 0.0 for grayscale
    vec3 col_saturated = mix(grayscale, col_linear_corrected, params.saturationAdjustment);
    col_saturated = clamp(col_saturated, 0.0, 1.0); // Clamp again after saturation
    // --- END SATURATION ADJUSTMENT ---

    // sRGB EOTF and output (using the saturated color)
    // This is correct if your swapchain is linear (_UNORM)
    outColor = vec4(srgb_eotf(col_saturated.r),
                    srgb_eotf(col_saturated.g),
                    srgb_eotf(col_saturated.b),
                    1.0);
}
