#include "Renderer.h"
#include <stdexcept>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <vector>
#include <array>
#include <sstream>
#include <iostream>

#define GL_CHECK(stmt) do { \
        stmt; \
        GLenum err; \
        while ((err = glGetError()) != GL_NO_ERROR) { \
            std::cerr << "OpenGL error 0x" << std::hex << err << std::dec \
                      << " at " << __FILE__ << ":" << __LINE__ \
                      << " - " << #stmt << std::endl; \
        } \
    } while (0)


const char* Renderer::computeShaderSrcOriginal = R"GLSL(#version 310 es
precision highp float;
precision highp int;
precision highp usampler2D;
precision highp image2D;
layout(local_size_x = 16, local_size_y = 16) in;
uniform highp usampler2D rawImageBufferTexture;
layout(binding = 1, rgba8) writeonly uniform highp image2D outImage;
uniform int   W, H, cfaType;
uniform float exposure, blackLevel, whiteLevel, invBlackWhiteRange;
uniform float gainR, gainG, gainB;
uniform mat3  CCM;
float srgb_eotf(float v) { v = clamp(v,0.0,1.0); return (v <= 0.0031308) ? v*12.92 : 1.055*pow(v,1.0/2.4) - 0.055; }
uint readU16_val(int x, int y) { x=clamp(x,0,W-1); y=clamp(y,0,H-1); return texelFetch(rawImageBufferTexture, ivec2(x,y), 0).r; }
float lin(uint v_u16){ float t=(float(v_u16)-blackLevel)*invBlackWhiteRange; return clamp(t*exposure,0.0,1.0);}
float interpG(int x,int y){ return 0.25*(lin(readU16_val(x+1,y))+lin(readU16_val(x-1,y))+lin(readU16_val(x,y+1))+lin(readU16_val(x,y-1))); }
float interpH(int x,int y){ return 0.5*(lin(readU16_val(x+1,y))+lin(readU16_val(x-1,y))); }
float interpV(int x,int y){ return 0.5*(lin(readU16_val(x,y+1))+lin(readU16_val(x,y-1))); }
float interpD(int x,int y){ return 0.25*(lin(readU16_val(x+1,y+1))+lin(readU16_val(x-1,y+1))+lin(readU16_val(x+1,y-1))+lin(readU16_val(x-1,y-1))); }
void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (p.x >= W || p.y >= H) return;
    int x = p.x; int y = p.y; bool ye = (y % 2) == 0; bool xe = (x % 2) == 0;
    float r_demosaiced=0.0, g_demosaiced=0.0, b_demosaiced=0.0;
    if (cfaType == 0) { if (ye) { if (xe) { b_demosaiced=lin(readU16_val(x,y)); g_demosaiced=interpG(x,y); r_demosaiced=interpD(x,y); } else { g_demosaiced=lin(readU16_val(x,y)); r_demosaiced=interpV(x,y); b_demosaiced=interpH(x,y); } } else { if (xe) { g_demosaiced=lin(readU16_val(x,y)); r_demosaiced=interpH(x,y); b_demosaiced=interpV(x,y); } else { r_demosaiced=lin(readU16_val(x,y)); g_demosaiced=interpG(x,y); b_demosaiced=interpD(x,y); } } }
    else if (cfaType == 1) { if (ye) { if (xe) { r_demosaiced=lin(readU16_val(x,y)); g_demosaiced=interpG(x,y); b_demosaiced=interpD(x,y); } else { g_demosaiced=lin(readU16_val(x,y)); r_demosaiced=interpH(x,y); b_demosaiced=interpV(x,y); } } else { if (xe) { g_demosaiced=lin(readU16_val(x,y)); r_demosaiced=interpV(x,y); b_demosaiced=interpH(x,y); } else { b_demosaiced=lin(readU16_val(x,y)); g_demosaiced=interpG(x,y); r_demosaiced=interpD(x,y); } } }
    else if (cfaType == 2) { if (ye) { if (xe) { g_demosaiced=lin(readU16_val(x,y)); r_demosaiced=interpV(x,y); b_demosaiced=interpH(x,y); } else { b_demosaiced=lin(readU16_val(x,y)); g_demosaiced=interpG(x,y); r_demosaiced=interpD(x,y); } } else { if (xe) { r_demosaiced=lin(readU16_val(x,y)); g_demosaiced=interpG(x,y); b_demosaiced=interpD(x,y); } else { g_demosaiced=lin(readU16_val(x,y)); r_demosaiced=interpH(x,y); b_demosaiced=interpV(x,y); } } }
    else { if (ye) { if (xe) { g_demosaiced=lin(readU16_val(x,y)); r_demosaiced=interpV(x,y); b_demosaiced=interpH(x,y); } else { r_demosaiced=lin(readU16_val(x,y)); g_demosaiced=interpG(x,y); b_demosaiced=interpD(x,y); } } else { if (xe) { b_demosaiced=lin(readU16_val(x,y)); g_demosaiced=interpG(x,y); r_demosaiced=interpD(x,y); } else { g_demosaiced=lin(readU16_val(x,y)); r_demosaiced=interpH(x,y); b_demosaiced=interpV(x,y); } } }
    float r_wb = clamp(r_demosaiced * gainR, 0.0, 1.0); float g_wb = clamp(g_demosaiced * gainG, 0.0, 1.0); float b_wb = clamp(b_demosaiced * gainB, 0.0, 1.0);
    vec3 col_linear_corrected = CCM * vec3(r_wb, g_wb, b_wb); col_linear_corrected = clamp(col_linear_corrected, 0.0, 1.0);
    imageStore(outImage, p, vec4(srgb_eotf(col_linear_corrected.r), srgb_eotf(col_linear_corrected.g), srgb_eotf(col_linear_corrected.b), 1.0));
})GLSL";

const char* Renderer::computeShaderSrcTiled = R"GLSL(#version 310 es
precision highp float; precision highp int; precision highp uint; precision highp usampler2D; precision highp image2D;
layout(local_size_x = 16, local_size_y = 16) in;
uniform highp usampler2D rawImageBufferTexture;
layout(binding = 1, rgba8) writeonly uniform highp image2D outImage;
uniform int   W, H, cfaType;
uniform float exposure, blackLevel, whiteLevel, invBlackWhiteRange;
uniform float gainR, gainG, gainB;
uniform mat3  CCM;
#define TILE_DIM_X 16
#define TILE_DIM_Y 16
#define HALO_SIZE 1
#define PADDED_TILE_DIM_X (TILE_DIM_X + 2 * HALO_SIZE)
#define PADDED_TILE_DIM_Y (TILE_DIM_Y + 2 * HALO_SIZE)
shared uint tile[PADDED_TILE_DIM_Y][PADDED_TILE_DIM_X];
float srgb_eotf(float v) { v = clamp(v,0.0,1.0); return (v <= 0.0031308) ? v*12.92 : 1.055*pow(v,1.0/2.4) - 0.055; }
uint read_global_u16_val(int gx, int gy) { gx = clamp(gx,0,W-1); gy = clamp(gy,0,H-1); return texelFetch(rawImageBufferTexture, ivec2(gx,gy), 0).r;}
float lin_from_tile_val(uint v_u16){ float t=(float(v_u16)-blackLevel)*invBlackWhiteRange; return clamp(t*exposure,0.0,1.0);}
#define PIXEL_VAL(dc,dr) lin_from_tile_val(tile[gl_LocalInvocationID.y+HALO_SIZE+(dr)][gl_LocalInvocationID.x+HALO_SIZE+(dc)])
void main() {
    ivec2 gP=ivec2(gl_GlobalInvocationID.xy); if(gP.x>=W||gP.y>=H)return;
    uint lx=gl_LocalInvocationID.x,ly=gl_LocalInvocationID.y; int gx_b=int(gl_WorkGroupID.x*TILE_DIM_X),gy_b=int(gl_WorkGroupID.y*TILE_DIM_Y);
    tile[ly+HALO_SIZE][lx+HALO_SIZE]=read_global_u16_val(gx_b+int(lx),gy_b+int(ly));
    if(lx<HALO_SIZE)tile[ly+HALO_SIZE][lx]=read_global_u16_val(gx_b+int(lx)-HALO_SIZE,gy_b+int(ly));
    if(lx>=TILE_DIM_X-HALO_SIZE)tile[ly+HALO_SIZE][lx+2*HALO_SIZE]=read_global_u16_val(gx_b+int(lx)+HALO_SIZE,gy_b+int(ly));
    if(ly<HALO_SIZE)tile[ly][lx+HALO_SIZE]=read_global_u16_val(gx_b+int(lx),gy_b+int(ly)-HALO_SIZE);
    if(ly>=TILE_DIM_Y-HALO_SIZE)tile[ly+2*HALO_SIZE][lx+HALO_SIZE]=read_global_u16_val(gx_b+int(lx),gy_b+int(ly)+HALO_SIZE);
    if(lx<HALO_SIZE&&ly<HALO_SIZE)tile[ly][lx]=read_global_u16_val(gx_b+int(lx)-HALO_SIZE,gy_b+int(ly)-HALO_SIZE);
    if(lx>=TILE_DIM_X-HALO_SIZE&&ly<HALO_SIZE)tile[ly][lx+2*HALO_SIZE]=read_global_u16_val(gx_b+int(lx)+HALO_SIZE,gy_b+int(ly)-HALO_SIZE);
    if(lx<HALO_SIZE&&ly>=TILE_DIM_Y-HALO_SIZE)tile[ly+2*HALO_SIZE][lx]=read_global_u16_val(gx_b+int(lx)-HALO_SIZE,gy_b+int(ly)+HALO_SIZE);
    if(lx>=TILE_DIM_X-HALO_SIZE&&ly>=TILE_DIM_Y-HALO_SIZE)tile[ly+2*HALO_SIZE][lx+2*HALO_SIZE]=read_global_u16_val(gx_b+int(lx)+HALO_SIZE,gy_b+int(ly)+HALO_SIZE);
    barrier();
    bool ye=(gP.y%2)==0,xe=(gP.x%2)==0; float r_d=0,g_d=0,b_d=0;
    float gi=0.25*(PIXEL_VAL(1,0)+PIXEL_VAL(-1,0)+PIXEL_VAL(0,1)+PIXEL_VAL(0,-1));
    float rh=0.5*(PIXEL_VAL(1,0)+PIXEL_VAL(-1,0)),bh=rh;
    float rv=0.5*(PIXEL_VAL(0,1)+PIXEL_VAL(0,-1)),bv=rv;
    float di=0.25*(PIXEL_VAL(1,1)+PIXEL_VAL(-1,1)+PIXEL_VAL(1,-1)+PIXEL_VAL(-1,-1));
    if(cfaType==0){if(ye){if(xe){b_d=PIXEL_VAL(0,0);g_d=gi;r_d=di;}else{g_d=PIXEL_VAL(0,0);r_d=bv;b_d=rh;}}else{if(xe){g_d=PIXEL_VAL(0,0);r_d=rh;b_d=bv;}else{r_d=PIXEL_VAL(0,0);g_d=gi;b_d=di;}}}
    else if(cfaType==1){if(ye){if(xe){r_d=PIXEL_VAL(0,0);g_d=gi;b_d=di;}else{g_d=PIXEL_VAL(0,0);r_d=rh;b_d=bv;}}else{if(xe){g_d=PIXEL_VAL(0,0);r_d=bv;b_d=rh;}else{b_d=PIXEL_VAL(0,0);g_d=gi;r_d=di;}}}
    else if(cfaType==2){if(ye){if(xe){g_d=PIXEL_VAL(0,0);r_d=bv;b_d=rh;}else{b_d=PIXEL_VAL(0,0);g_d=gi;r_d=di;}}else{if(xe){r_d=PIXEL_VAL(0,0);g_d=gi;b_d=di;}else{g_d=PIXEL_VAL(0,0);r_d=rh;b_d=bv;}}}
    else{if(ye){if(xe){g_d=PIXEL_VAL(0,0);r_d=rh;b_d=bv;}else{r_d=PIXEL_VAL(0,0);g_d=gi;b_d=di;}}else{if(xe){b_d=PIXEL_VAL(0,0);g_d=gi;r_d=di;}else{g_d=PIXEL_VAL(0,0);r_d=bv;b_d=rh;}}}
    float r_wb=clamp(r_d*gainR,0.0,1.0); float g_wb=clamp(g_d*gainG,0.0,1.0); float b_wb=clamp(b_d*gainB,0.0,1.0);
    vec3 col_c=CCM*vec3(r_wb,g_wb,b_wb); col_c=clamp(col_c,0.0,1.0);
    imageStore(outImage,gP,vec4(srgb_eotf(col_c.r),srgb_eotf(col_c.g),srgb_eotf(col_c.b),1.0));
})GLSL";

const char* Renderer::vertexShaderSrc = R"GLSL(#version 300 es
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aTex;
out vec2 Tex;
uniform vec2 uScale;
uniform vec2 uOffset;
void main(){ Tex = aTex; gl_Position = vec4(aPos * uScale + uOffset, 0.0, 1.0); }
)GLSL";

const char* Renderer::fragmentShaderSrc = R"GLSL(#version 300 es
precision mediump float;
in vec2 Tex;
out vec4 FragColor;
uniform sampler2D screenTexture;
void main(){ FragColor=texture(screenTexture, vec2(Tex.x, 1.0-Tex.y)); }
)GLSL";

Renderer::Renderer() {}
Renderer::~Renderer() { cleanup(); }

float Renderer::getPanX() const { return m_panX; }
float Renderer::getPanY() const { return m_panY; }
int Renderer::getImageWidth() const { return m_currentW; }
int Renderer::getImageHeight() const { return m_currentH; }

bool Renderer::initialize() {
#ifdef USE_COMPUTE_SHADER_TILING
    const char* actualComputeShaderSrc = computeShaderSrcTiled;
#else
    const char* actualComputeShaderSrc = computeShaderSrcOriginal;
#endif

    m_quadProg = createShaderProgram(vertexShaderSrc, fragmentShaderSrc);
    m_compProg = createComputeProgram(actualComputeShaderSrc);

    if (!m_quadProg || !m_compProg) { cleanup(); return false; }
    m_quadVAO = createQuadVAO();
    if (m_quadVAO == 0) { cleanup(); return false; }

    GL_CHECK(glUseProgram(m_quadProg));
    GL_CHECK(glUniform1i(glGetUniformLocation(m_quadProg, "screenTexture"), 0));

    GL_CHECK(glGenBuffers(1, &m_rawDataPBO));
    GL_CHECK(glGenTextures(1, &m_rawDataTex));
    if (m_rawDataPBO == 0 || m_rawDataTex == 0) { cleanup(); return false; }

    GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_rawDataTex));
    GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, 1, 1, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, nullptr));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));

    GL_CHECK(glGenTextures(1, &m_outputDisplayTex));
    if (m_outputDisplayTex == 0) { cleanup(); return false; }

    GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_outputDisplayTex));
    GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));

    m_currentW = 0; m_currentH = 0;
    return true;
}

void Renderer::cleanup() {
    if (m_rawDataPBO) { GL_CHECK(glDeleteBuffers(1, &m_rawDataPBO)); m_rawDataPBO = 0; }
    if (m_rawDataTex) { GL_CHECK(glDeleteTextures(1, &m_rawDataTex)); m_rawDataTex = 0; }
    if (m_outputDisplayTex) { GL_CHECK(glDeleteTextures(1, &m_outputDisplayTex)); m_outputDisplayTex = 0; }
    if (m_quadVAO) { GL_CHECK(glDeleteVertexArrays(1, &m_quadVAO)); m_quadVAO = 0; }
    if (m_quadProg) { GL_CHECK(glDeleteProgram(m_quadProg)); m_quadProg = 0; }
    if (m_compProg) { GL_CHECK(glDeleteProgram(m_compProg)); m_compProg = 0; }
    m_currentW = m_currentH = 0;
}

void Renderer::renderFrame(const std::vector<uint16_t>& rawData,
    const nlohmann::json& frameMetadata,
    double staticBlack, double staticWhite,
    int cfaType)
{
    int w = frameMetadata.value("width", 0);
    int h = frameMetadata.value("height", 0);
    size_t expected_pixels = static_cast<size_t>(w) * h;

    if (w <= 0 || h <= 0 || rawData.size() < expected_pixels) {
        GL_CHECK(glClearColor(1.0f, 0.0f, 1.0f, 1.0f));
        GL_CHECK(glClear(GL_COLOR_BUFFER_BIT));
        return;
    }

    int current_vp[4];
    GL_CHECK(glGetIntegerv(GL_VIEWPORT, current_vp));


    if (w != m_currentW || h != m_currentH) {
        GL_CHECK(glFinish());

        /* raw data texture – mutable, so a simple redefine is fine */
        GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_rawDataTex));
        GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI,
            w, h, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, nullptr));

        /* PBO size matches the new frame */
        GL_CHECK(glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_rawDataPBO));
        GL_CHECK(glBufferData(GL_PIXEL_UNPACK_BUFFER,
            static_cast<GLsizeiptr>(w) * h * sizeof(uint16_t),
            nullptr, GL_DYNAMIC_DRAW));
        GL_CHECK(glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0));

        /* recreate the **immutable** display texture with the new size */
        GL_CHECK(glDeleteTextures(1, &m_outputDisplayTex));
        GL_CHECK(glGenTextures(1, &m_outputDisplayTex));
        GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_outputDisplayTex));
        GL_CHECK(glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
        GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));

        m_currentW = w;
        m_currentH = h;
    }

    GL_CHECK(glViewport(0, 0, current_vp[2], current_vp[3]));

    GL_CHECK(glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_rawDataPBO));
    GL_CHECK(glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, expected_pixels * sizeof(uint16_t), rawData.data()));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_rawDataTex));
    GL_CHECK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
        GL_RED_INTEGER, GL_UNSIGNED_SHORT, (const void*)0));
    GL_CHECK(glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));

    if (m_compProg == 0) { return; }
    GL_CHECK(glUseProgram(m_compProg));

    GL_CHECK(glUniform1i(glGetUniformLocation(m_compProg, "W"), w));
    GL_CHECK(glUniform1i(glGetUniformLocation(m_compProg, "H"), h));
    GL_CHECK(glUniform1i(glGetUniformLocation(m_compProg, "cfaType"), cfaType));
    GL_CHECK(glUniform1f(glGetUniformLocation(m_compProg, "exposure"), 1.f));

    float blackLvl = static_cast<float>(staticBlack);
    if (frameMetadata.contains("dynamicBlackLevel")) {
        const auto& jb = frameMetadata["dynamicBlackLevel"];
        if (jb.is_array() && !jb.empty()) {
            double avg = 0; std::size_t count = 0;
            for (const auto& val : jb) { if (val.is_number()) { avg += val.get<double>(); count++; } }
            if (count > 0) blackLvl = static_cast<float>(avg / count);
        }
        else if (jb.is_number()) { blackLvl = jb.get<float>(); }
    }
    float whiteLvl = static_cast<float>(staticWhite);
    if (frameMetadata.contains("dynamicWhiteLevel") && frameMetadata["dynamicWhiteLevel"].is_number()) {
        whiteLvl = frameMetadata["dynamicWhiteLevel"].get<float>();
    }
    GL_CHECK(glUniform1f(glGetUniformLocation(m_compProg, "blackLevel"), blackLvl));
    GL_CHECK(glUniform1f(glGetUniformLocation(m_compProg, "whiteLevel"), whiteLvl));
    float range = whiteLvl - blackLvl; if (range <= 1e-5f) range = 1.0f;
    float invBlackWhiteRange = 1.0f / range;
    GL_CHECK(glUniform1f(glGetUniformLocation(m_compProg, "invBlackWhiteRange"), invBlackWhiteRange));

    auto asn_json = frameMetadata.value("asShotNeutral", nlohmann::json::array({ 1.0,1.0,1.0 }));
    std::vector<double> asn = { 1.0,1.0,1.0 };
    if (asn_json.is_array() && asn_json.size() >= 3) {
        bool v = true; std::vector<double> t;
        for (const auto& e : asn_json) if (e.is_number())t.push_back(e.get<double>()); else { v = false;break; }
        if (v && t.size() >= 3)asn = t;
    }
    float gainR_val = 1.f, gainG_val = 1.f, gainB_val = 1.f;
    if (asn.size() >= 3 && !std::isnan(asn[0]) && !std::isnan(asn[1]) && !std::isnan(asn[2]) &&
        asn[0] > 1e-6 && asn[1] > 1e-6 && asn[2] > 1e-6) {
        gainR_val = static_cast<float>(asn[1] / asn[0]); gainB_val = static_cast<float>(asn[1] / asn[2]);
    }
    GL_CHECK(glUniform1f(glGetUniformLocation(m_compProg, "gainR"), gainR_val));
    GL_CHECK(glUniform1f(glGetUniformLocation(m_compProg, "gainG"), gainG_val));
    GL_CHECK(glUniform1f(glGetUniformLocation(m_compProg, "gainB"), gainB_val));

    std::array<float, 9> ccm_data_to_upload = { 1,0,0, 0,1,0, 0,0,1 };
#ifdef USE_METADATA_CCM
    nlohmann::json ccm_json_meta;
    if (frameMetadata.contains("ColorMatrix2") && frameMetadata["ColorMatrix2"].is_array() && frameMetadata["ColorMatrix2"].size() == 9) {
        ccm_json_meta = frameMetadata["ColorMatrix2"];
    }
    else if (frameMetadata.contains("ColorMatrix") && frameMetadata["ColorMatrix"].is_array() && frameMetadata["ColorMatrix"].size() == 9) {
        ccm_json_meta = frameMetadata["ColorMatrix"];
    }
    if (!ccm_json_meta.is_null()) {
        bool valid_ccm = true;
        for (size_t i = 0; i < 9; ++i) {
            if (ccm_json_meta[i].is_number()) {
                ccm_data_to_upload[i] = ccm_json_meta[i].get<float>();
            }
            else { valid_ccm = false; break; }
        }
        if (!valid_ccm) ccm_data_to_upload = { 1,0,0, 0,1,0, 0,0,1 };
    }
#endif
    GL_CHECK(glUniformMatrix3fv(glGetUniformLocation(m_compProg, "CCM"), 1, GL_FALSE, ccm_data_to_upload.data()));


    GLint rawDataTexUnit = 0;
    GL_CHECK(glActiveTexture(GL_TEXTURE0 + rawDataTexUnit));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_rawDataTex));
    GLint loc = glGetUniformLocation(m_compProg, "rawImageBufferTexture");
    if (loc != -1) {
        GL_CHECK(glUniform1i(loc, rawDataTexUnit));
    }
    GL_CHECK(glBindImageTexture(1, m_outputDisplayTex, 0, GL_FALSE, 0,
        GL_WRITE_ONLY, GL_RGBA8));


    const GLuint GROUP_X = 16;
    const GLuint GROUP_Y = 16;
    GLuint groupsX = (w + GROUP_X - 1) / GROUP_X;
    GLuint groupsY = (h + GROUP_Y - 1) / GROUP_Y;

    GL_CHECK(glDispatchCompute(groupsX, groupsY, 1));
    GL_CHECK(glMemoryBarrier(GL_ALL_BARRIER_BITS));


    if (m_quadProg == 0 || m_quadVAO == 0) { return; }
    float winW = static_cast<float>(current_vp[2]);
    float winH = static_cast<float>(current_vp[3]);

    float imgW = static_cast<float>(w);
    float imgH = static_cast<float>(h);

    float sX = 1.0f, sY = 1.0f;
    if (winW > 0 && winH > 0 && imgW > 0 && imgH > 0) {
        if (m_zoomNativePixels) {
            sX = imgW / winW;
            sY = imgH / winH;
        }
        else {
            float windowAspectRatio = winW / winH;
            float imageAspectRatio = imgW / imgH;
            if (windowAspectRatio > imageAspectRatio) {
                sX = imageAspectRatio / windowAspectRatio;
                sY = 1.0f;
            }
            else {
                sX = 1.0f;
                sY = windowAspectRatio / imageAspectRatio;
            }
        }
    }
    float nX = (winW > 0 && m_zoomNativePixels) ? (2.f * m_panX / winW) : 0.f;
    float nY = (winH > 0 && m_zoomNativePixels) ? (-2.f * m_panY / winH) : 0.f;

    GL_CHECK(glUseProgram(m_quadProg));
    GL_CHECK(glUniform2f(glGetUniformLocation(m_quadProg, "uScale"), sX, sY));
    GL_CHECK(glUniform2f(glGetUniformLocation(m_quadProg, "uOffset"), nX, nY));
    GL_CHECK(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT));
    GL_CHECK(glActiveTexture(GL_TEXTURE0));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_outputDisplayTex));
    GL_CHECK(glBindVertexArray(m_quadVAO));
    GL_CHECK(glDrawArrays(GL_TRIANGLES, 0, 6));

    GL_CHECK(glBindVertexArray(0));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
    GL_CHECK(glUseProgram(0));
}

bool Renderer::checkShader(GLuint sh, const std::string& type) {
    GLint ok;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint logLen;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> logBuf(logLen > 0 ? logLen : 1024);
        glGetShaderInfoLog(sh, static_cast<GLsizei>(logBuf.size()), nullptr, logBuf.data());
        std::cerr << "Shader(" << type << ") compile error:\n" << logBuf.data() << std::endl;
    } return ok == GL_TRUE;
}
bool Renderer::checkProgram(GLuint pr) {
    GLint ok;
    glGetProgramiv(pr, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint logLen;
        glGetProgramiv(pr, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> logBuf(logLen > 0 ? logLen : 1024);
        glGetProgramInfoLog(pr, static_cast<GLsizei>(logBuf.size()), nullptr, logBuf.data());
        std::cerr << "Program link error:\n" << logBuf.data() << std::endl;
    } return ok == GL_TRUE;
}
GLuint Renderer::createShaderProgram(const char* vs_src, const char* fs_src) {
    GLuint v_shader = glCreateShader(GL_VERTEX_SHADER); // Wrapped by GL_CHECK in caller if needed
    glShaderSource(v_shader, 1, &vs_src, nullptr);
    glCompileShader(v_shader);
    if (!checkShader(v_shader, "VS")) { glDeleteShader(v_shader); return 0; }

    GLuint f_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(f_shader, 1, &fs_src, nullptr);
    glCompileShader(f_shader);
    if (!checkShader(f_shader, "FS")) { glDeleteShader(v_shader); glDeleteShader(f_shader); return 0; }

    GLuint program = glCreateProgram();
    glAttachShader(program, v_shader);
    glAttachShader(program, f_shader);
    glLinkProgram(program);
    glDeleteShader(v_shader);
    glDeleteShader(f_shader);
    if (!checkProgram(program)) { glDeleteProgram(program); return 0; }
    return program;
}
GLuint Renderer::createComputeProgram(const char* cs_source_text) {
    GLuint shader_id = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader_id, 1, &cs_source_text, nullptr);
    glCompileShader(shader_id);
    if (!checkShader(shader_id, "CS")) {
        glDeleteShader(shader_id); return 0;
    }
    GLuint program_id = glCreateProgram();
    glAttachShader(program_id, shader_id);
    glLinkProgram(program_id);
    glDeleteShader(shader_id);
    if (!checkProgram(program_id)) { glDeleteProgram(program_id); return 0; }
    return program_id;
}
GLuint Renderer::createQuadVAO() {
    float v_data[] = {
        -1.f,  1.f, 0.f, 1.f,
        -1.f, -1.f, 0.f, 0.f,
         1.f, -1.f, 1.f, 0.f,

        -1.f,  1.f, 0.f, 1.f,
         1.f, -1.f, 1.f, 0.f,
         1.f,  1.f, 1.f, 1.f
    };
    GLuint vao = 0, vbo = 0;
    GL_CHECK(glGenVertexArrays(1, &vao));
    GL_CHECK(glGenBuffers(1, &vbo));
    if (vao == 0 || vbo == 0) { if (vao) { GL_CHECK(glDeleteVertexArrays(1, &vao)); } if (vbo) { GL_CHECK(glDeleteBuffers(1, &vbo)); } return 0; }
    GL_CHECK(glBindVertexArray(vao));
    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, vbo));
    GL_CHECK(glBufferData(GL_ARRAY_BUFFER, sizeof(v_data), v_data, GL_STATIC_DRAW));
    GL_CHECK(glEnableVertexAttribArray(0));
    GL_CHECK(glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0));
    GL_CHECK(glEnableVertexAttribArray(1));
    GL_CHECK(glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float))));
    GL_CHECK(glBindVertexArray(0));
    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, 0));
    return vao;
}

int Renderer::getCfaType(const std::string& c) {
    std::string upper_cfa = c;
    std::transform(upper_cfa.begin(), upper_cfa.end(), upper_cfa.begin(), [](unsigned char c_) {return static_cast<char>(std::toupper(c_));});
    if (upper_cfa == "BGGR") return 0;
    if (upper_cfa == "RGGB") return 1;
    if (upper_cfa == "GBRG") return 2;
    if (upper_cfa == "GRBG") return 3;
    if (c != "BGGR" && !c.empty()) {}
    return 0;
}
void Renderer::setZoomNativePixels(bool n) { m_zoomNativePixels = n; }
void Renderer::setPanOffsets(float x, float y) { m_panX = x; m_panY = y; }
void Renderer::resetPanOffsets() { m_panX = 0.0f; m_panY = 0.0f; }
void Renderer::setOrientation(int orientation) { m_orientation = orientation % 4; }
void Renderer::resetDimensions() { m_currentW = 0; m_currentH = 0; }