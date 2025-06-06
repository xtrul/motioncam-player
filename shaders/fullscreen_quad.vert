// --- START OF FILE shaders/fullscreen_quad.vert ---
#version 450

layout(location = 0) out vec2 outTexCoord;

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
    outTexCoord = texCoords[gl_VertexIndex];
}
// --- END OF FILE shaders/fullscreen_quad.vert ---