#version 450

layout(location = 0) out vec2 outTexCoord;

layout(binding = 1) uniform ShaderParams {
    int W; int H; int cfaType; float exposure;
    float blackLevel; float whiteLevel; float invBlackWhiteRange;
    float gainR; float gainG; float gainB;
    mat4 CCM; float saturationAdjustment; int orientationDegrees; int demosaicMode;
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

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    vec2 tc = texCoords[gl_VertexIndex];
    if (params.orientationDegrees == 90) {
        tc = vec2(tc.y, 1.0 - tc.x);
    } else if (params.orientationDegrees == 180) {
        tc = vec2(1.0 - tc.x, 1.0 - tc.y);
    } else if (params.orientationDegrees == 270) {
        tc = vec2(1.0 - tc.y, tc.x);
    }
    outTexCoord = tc;
}
