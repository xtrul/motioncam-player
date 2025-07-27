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

float vh(int x, int y){
    float xs=0.0, ys=0.0;
    for(int i=-1;i<=1;i++){
        xs += lin(readU16_val(x+i,y-3)) - 3.0*lin(readU16_val(x+i,y-2)) - lin(readU16_val(x+i,y-1)) + 6.0*lin(readU16_val(x+i,y)) - lin(readU16_val(x+i,y+1)) - 3.0*lin(readU16_val(x+i,y+2)) + lin(readU16_val(x+i,y+3));
        ys += lin(readU16_val(x-3,y+i)) - 3.0*lin(readU16_val(x-2,y+i)) - lin(readU16_val(x-1,y+i)) + 6.0*lin(readU16_val(x,y+i)) - lin(readU16_val(x+1,y+i)) - 3.0*lin(readU16_val(x+2,y+i)) + lin(readU16_val(x+3,y+i));
    }
    xs*=xs; ys*=ys;
    return xs / (1e-5 + xs + ys);
}

float greenAtRB(int x, int y){
    float v = vh(x,y);
    float n = 0.25*(vh(x-1,y-1)+vh(x+1,y-1)+vh(x-1,y+1)+vh(x+1,y+1));
    float w = abs(0.5 - v) < abs(0.5 - n) ? n : v;
    float eps = 1e-5;
    float Ng = eps + abs(lin(readU16_val(x, y-1)) - lin(readU16_val(x, y+1))) + abs(lin(readU16_val(x, y)) - lin(readU16_val(x, y-2)));
    float Sg = eps + abs(lin(readU16_val(x, y-1)) - lin(readU16_val(x, y+1))) + abs(lin(readU16_val(x, y)) - lin(readU16_val(x, y+2)));
    float Eg = eps + abs(lin(readU16_val(x-1, y)) - lin(readU16_val(x+1, y))) + abs(lin(readU16_val(x, y)) - lin(readU16_val(x+2, y)));
    float Wg = eps + abs(lin(readU16_val(x-1, y)) - lin(readU16_val(x+1, y))) + abs(lin(readU16_val(x, y)) - lin(readU16_val(x-2, y)));
    float gv = (Sg*(lin(readU16_val(x, y-1))+lin(readU16_val(x, y+1)))*0.5 + Ng*(lin(readU16_val(x, y-1))+lin(readU16_val(x, y+1)))*0.5)/(Ng+Sg);
    float gh = (Eg*(lin(readU16_val(x-1, y))+lin(readU16_val(x+1, y)))*0.5 + Wg*(lin(readU16_val(x-1, y))+lin(readU16_val(x+1, y)))*0.5)/(Eg+Wg);
    return mix(gv,gh,w);
}

float greenAt(int x, int y){
    bool ye = (y % 2) == 0;
    bool xe = (x % 2) == 0;
    if(params.cfaType==0) return ye? (xe? greenAtRB(x,y): lin(readU16_val(x,y))) : (xe? lin(readU16_val(x,y)): greenAtRB(x,y));
    if(params.cfaType==1) return ye? (xe? lin(readU16_val(x,y)): greenAtRB(x,y)) : (xe? greenAtRB(x,y): lin(readU16_val(x,y)));
    if(params.cfaType==2) return ye? (xe? lin(readU16_val(x,y)): greenAtRB(x,y)) : (xe? greenAtRB(x,y): lin(readU16_val(x,y)));
    return ye? (xe? greenAtRB(x,y): lin(readU16_val(x,y))) : (xe? lin(readU16_val(x,y)): greenAtRB(x,y));
}

void main() {
    ivec2 p = ivec2(inTexCoord * vec2(params.W, params.H));
    
    if (p.x >= params.W || p.y >= params.H || p.x < 0 || p.y < 0) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0); 
        return;
    }

    int x = p.x;
    int y = p.y;
    bool ye = (y % 2) == 0; 
    bool xe = (x % 2) == 0; 

    float r_demosaiced = 0.0, g_demosaiced = 0.0, b_demosaiced = 0.0;

    float rcd_r = 0.0, rcd_g = 0.0, rcd_b = 0.0;
    if(params.cfaType == 0){ // BGGR
        if(ye){
            if(xe){
                rcd_g = greenAtRB(x,y); rcd_b = lin(readU16_val(x,y));
                float dr = (lin(readU16_val(x-1,y-1))-greenAt(x-1,y-1)+lin(readU16_val(x+1,y-1))-greenAt(x+1,y-1)+lin(readU16_val(x-1,y+1))-greenAt(x-1,y+1)+lin(readU16_val(x+1,y+1))-greenAt(x+1,y+1))*0.25;
                rcd_r = rcd_g + dr;
            }else{
                rcd_g = lin(readU16_val(x,y));
                float dr=(lin(readU16_val(x,y-1))-greenAt(x,y-1)+lin(readU16_val(x,y+1))-greenAt(x,y+1))*0.5;
                float db=(lin(readU16_val(x-1,y))-greenAt(x-1,y)+lin(readU16_val(x+1,y))-greenAt(x+1,y))*0.5;
                rcd_r = rcd_g + dr; rcd_b = rcd_g + db;
            }
        }else{
            if(xe){
                rcd_g = lin(readU16_val(x,y));
                float dr=(lin(readU16_val(x-1,y))-greenAt(x-1,y)+lin(readU16_val(x+1,y))-greenAt(x+1,y))*0.5;
                float db=(lin(readU16_val(x,y-1))-greenAt(x,y-1)+lin(readU16_val(x,y+1))-greenAt(x,y+1))*0.5;
                rcd_r = rcd_g + dr; rcd_b = rcd_g + db;
            }else{
                rcd_g = greenAtRB(x,y); rcd_r = lin(readU16_val(x,y));
                float db=(lin(readU16_val(x-1,y-1))-greenAt(x-1,y-1)+lin(readU16_val(x+1,y-1))-greenAt(x+1,y-1)+lin(readU16_val(x-1,y+1))-greenAt(x-1,y+1)+lin(readU16_val(x+1,y+1))-greenAt(x+1,y+1))*0.25;
                rcd_b = rcd_g + db;
            }
        }
    }else if(params.cfaType == 1){ // RGGB
        if(ye){
            if(xe){
                rcd_g = greenAtRB(x,y); rcd_r = lin(readU16_val(x,y));
                float db=(lin(readU16_val(x-1,y-1))-greenAt(x-1,y-1)+lin(readU16_val(x+1,y-1))-greenAt(x+1,y-1)+lin(readU16_val(x-1,y+1))-greenAt(x-1,y+1)+lin(readU16_val(x+1,y+1))-greenAt(x+1,y+1))*0.25;
                rcd_b = rcd_g + db;
            }else{
                rcd_g = lin(readU16_val(x,y));
                float dr=(lin(readU16_val(x-1,y))-greenAt(x-1,y)+lin(readU16_val(x+1,y))-greenAt(x+1,y))*0.5;
                float db=(lin(readU16_val(x,y-1))-greenAt(x,y-1)+lin(readU16_val(x,y+1))-greenAt(x,y+1))*0.5;
                rcd_r = rcd_g + dr; rcd_b = rcd_g + db;
            }
        }else{
            if(xe){
                rcd_g = lin(readU16_val(x,y));
                float dr=(lin(readU16_val(x,y-1))-greenAt(x,y-1)+lin(readU16_val(x,y+1))-greenAt(x,y+1))*0.5;
                float db=(lin(readU16_val(x-1,y))-greenAt(x-1,y)+lin(readU16_val(x+1,y))-greenAt(x+1,y))*0.5;
                rcd_r = rcd_g + dr; rcd_b = rcd_g + db;
            }else{
                rcd_g = greenAtRB(x,y); rcd_b = lin(readU16_val(x,y));
                float dr=(lin(readU16_val(x-1,y-1))-greenAt(x-1,y-1)+lin(readU16_val(x+1,y-1))-greenAt(x+1,y-1)+lin(readU16_val(x-1,y+1))-greenAt(x-1,y+1)+lin(readU16_val(x+1,y+1))-greenAt(x+1,y+1))*0.25;
                rcd_r = rcd_g + dr;
            }
        }
    }else if(params.cfaType == 2){ // GBRG
        if(ye){
            if(xe){
                rcd_g = lin(readU16_val(x,y));
                float db=(lin(readU16_val(x-1,y))-greenAt(x-1,y)+lin(readU16_val(x+1,y))-greenAt(x+1,y))*0.5;
                float dr=(lin(readU16_val(x,y-1))-greenAt(x,y-1)+lin(readU16_val(x,y+1))-greenAt(x,y+1))*0.5;
                rcd_b = rcd_g + db; rcd_r = rcd_g + dr;
            }else{
                rcd_g = greenAtRB(x,y); rcd_b = lin(readU16_val(x,y));
                float dr=(lin(readU16_val(x-1,y-1))-greenAt(x-1,y-1)+lin(readU16_val(x+1,y-1))-greenAt(x+1,y-1)+lin(readU16_val(x-1,y+1))-greenAt(x-1,y+1)+lin(readU16_val(x+1,y+1))-greenAt(x+1,y+1))*0.25;
                rcd_r = rcd_g + dr;
            }
        }else{
            if(xe){
                rcd_g = greenAtRB(x,y); rcd_r = lin(readU16_val(x,y));
                float db=(lin(readU16_val(x-1,y-1))-greenAt(x-1,y-1)+lin(readU16_val(x+1,y-1))-greenAt(x+1,y-1)+lin(readU16_val(x-1,y+1))-greenAt(x-1,y+1)+lin(readU16_val(x+1,y+1))-greenAt(x+1,y+1))*0.25;
                rcd_b = rcd_g + db;
            }else{
                rcd_g = lin(readU16_val(x,y));
                float dr=(lin(readU16_val(x-1,y))-greenAt(x-1,y)+lin(readU16_val(x+1,y))-greenAt(x+1,y))*0.5;
                float db=(lin(readU16_val(x,y-1))-greenAt(x,y-1)+lin(readU16_val(x,y+1))-greenAt(x,y+1))*0.5;
                rcd_r = rcd_g + dr; rcd_b = rcd_g + db;
            }
        }
    }else{ // GRBG
        if(ye){
            if(xe){
                rcd_g = lin(readU16_val(x,y));
                float dr=(lin(readU16_val(x-1,y))-greenAt(x-1,y)+lin(readU16_val(x+1,y))-greenAt(x+1,y))*0.5;
                float db=(lin(readU16_val(x,y-1))-greenAt(x,y-1)+lin(readU16_val(x,y+1))-greenAt(x,y+1))*0.5;
                rcd_r = rcd_g + dr; rcd_b = rcd_g + db;
            }else{
                rcd_g = greenAtRB(x,y); rcd_r = lin(readU16_val(x,y));
                float db=(lin(readU16_val(x-1,y-1))-greenAt(x-1,y-1)+lin(readU16_val(x+1,y-1))-greenAt(x+1,y-1)+lin(readU16_val(x-1,y+1))-greenAt(x-1,y+1)+lin(readU16_val(x+1,y+1))-greenAt(x+1,y+1))*0.25;
                rcd_b = rcd_g + db;
            }
        }else{
            if(xe){
                rcd_g = greenAtRB(x,y); rcd_b = lin(readU16_val(x,y));
                float dr=(lin(readU16_val(x-1,y-1))-greenAt(x-1,y-1)+lin(readU16_val(x+1,y-1))-greenAt(x+1,y-1)+lin(readU16_val(x-1,y+1))-greenAt(x-1,y+1)+lin(readU16_val(x+1,y+1))-greenAt(x+1,y+1))*0.25;
                rcd_r = rcd_g + dr;
            }else{
                rcd_g = lin(readU16_val(x,y));
                float dr=(lin(readU16_val(x,y-1))-greenAt(x,y-1)+lin(readU16_val(x,y+1))-greenAt(x,y+1))*0.5;
                float db=(lin(readU16_val(x-1,y))-greenAt(x-1,y)+lin(readU16_val(x+1,y))-greenAt(x+1,y))*0.5;
                rcd_r = rcd_g + dr; rcd_b = rcd_g + db;
            }
        }
    }

    r_demosaiced = rcd_r;
    g_demosaiced = rcd_g;
    b_demosaiced = rcd_b;

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
