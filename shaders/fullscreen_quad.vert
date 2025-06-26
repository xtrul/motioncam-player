// --- START OF FILE shaders/fullscreen_quad.vert ---
#version 450

layout(location = 0) out vec2 outTexCoord;

layout(binding = 1) uniform ShaderParams {
    int W;
    int H;
    int cfaType;
    float exposure;
    float blackLevel;
    float whiteLevel;
    float invBlackWhiteRange;
    float gainR;
    float gainG;
    float gainB;
    mat4 CCM;
    float saturationAdjustment;
    int rotation;
} params;

// Fullscreen quad/triangle vertices. No actual vertex buffer needed.
// Two triangles covering the screen.
vec2 positions[6] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2(-1.0,  1.0),
    vec2(-1.0,  1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0)
);

// UVs to sample the entire texture
vec2 texCoords[6] = vec2[](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(0.0, 1.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0)
);

vec2 rotatePos(vec2 p){
    if(params.rotation==1) return vec2(p.y,-p.x);
    if(params.rotation==2) return vec2(-p.x,-p.y);
    if(params.rotation==3) return vec2(-p.y,p.x);
    return p;
}
vec2 rotateTex(vec2 t){
    if(params.rotation==1) return vec2(t.y,1.0-t.x);
    if(params.rotation==2) return vec2(1.0-t.x,1.0-t.y);
    if(params.rotation==3) return vec2(1.0-t.y,t.x);
    return t;
}
void main() {
    gl_Position = vec4(rotatePos(positions[gl_VertexIndex]), 0.0, 1.0);
    outTexCoord = rotateTex(texCoords[gl_VertexIndex]);
}
// --- END OF FILE shaders/fullscreen_quad.vert ---
