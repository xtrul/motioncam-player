// --- START OF FILE shaders/image_process.frag ---
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
    // --- ADD SATURATION UNIFORM ---
    float saturationAdjustment; // e.g., 1.0 for no change, 1.25 for +25%
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

    // ... (your existing demosaicing logic remains the same) ...
    if (params.cfaType == 0) { // BGGR
        if (ye) { 
            if (xe) { 
                b_demosaiced = lin(readU16_val(x, y));
                g_demosaiced = interpG(x, y);
                r_demosaiced = interpD(x, y);
            } else { 
                g_demosaiced = lin(readU16_val(x, y));
                r_demosaiced = interpV(x, y); 
                b_demosaiced = interpH(x, y); 
            }
        } else { 
            if (xe) { 
                g_demosaiced = lin(readU16_val(x, y));
                r_demosaiced = interpH(x, y); 
                b_demosaiced = interpV(x, y); 
            } else { 
                r_demosaiced = lin(readU16_val(x, y));
                g_demosaiced = interpG(x, y);
                b_demosaiced = interpD(x, y);
            }
        }
    } else if (params.cfaType == 1) { // RGGB
        if (ye) {
            if (xe) { 
                r_demosaiced = lin(readU16_val(x, y));
                g_demosaiced = interpG(x, y);
                b_demosaiced = interpD(x, y);
            } else { 
                g_demosaiced = lin(readU16_val(x, y));
                r_demosaiced = interpH(x, y);
                b_demosaiced = interpV(x, y);
            }
        } else {
            if (xe) { 
                g_demosaiced = lin(readU16_val(x, y));
                r_demosaiced = interpV(x, y);
                b_demosaiced = interpH(x, y);
            } else { 
                b_demosaiced = lin(readU16_val(x, y));
                g_demosaiced = interpG(x, y);
                r_demosaiced = interpD(x, y);
            }
        }
    } else if (params.cfaType == 2) { // GBRG
         if (ye) {
            if (xe) { 
                g_demosaiced = lin(readU16_val(x,y));
                r_demosaiced = interpV(x,y);
                b_demosaiced = interpH(x,y);
            } else { 
                b_demosaiced = lin(readU16_val(x,y));
                g_demosaiced = interpG(x,y);
                r_demosaiced = interpD(x,y);
            }
        } else {
            if (xe) { 
                r_demosaiced = lin(readU16_val(x,y));
                g_demosaiced = interpG(x,y);
                b_demosaiced = interpD(x,y);
            } else { 
                g_demosaiced = lin(readU16_val(x,y));
                r_demosaiced = interpH(x,y);
                b_demosaiced = interpV(x,y);
            }
        }
    } else { // GRBG (cfaType == 3 or default)
        if (ye) {
            if (xe) { 
                g_demosaiced = lin(readU16_val(x,y));
                r_demosaiced = interpH(x,y);
                b_demosaiced = interpV(x,y);
            } else { 
                r_demosaiced = lin(readU16_val(x,y));
                g_demosaiced = interpG(x,y);
                b_demosaiced = interpD(x,y);
            }
        } else {
            if (xe) { 
                b_demosaiced = lin(readU16_val(x,y));
                g_demosaiced = interpG(x,y);
                r_demosaiced = interpD(x,y);
            } else { 
                g_demosaiced = lin(readU16_val(x,y));
                r_demosaiced = interpV(x,y);
                b_demosaiced = interpH(x,y);
            }
        }
    }

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
// --- END OF FILE shaders/image_process.frag ---