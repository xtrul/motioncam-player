#version 450

layout(binding = 0) uniform sampler2D u_tex;

layout(push_constant) uniform PreviewParams {
    float blackNorm;
    float whiteNorm;
} pc;

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(u_tex, inTexCoord);
}
