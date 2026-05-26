#version 450

layout(push_constant) uniform PushConstants {
    vec2 viewport;
    uint use_texture;
} pc;

layout(binding = 0) uniform sampler2D tex0;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    vec4 texel = pc.use_texture != 0 ? texture(tex0, v_uv) : vec4(1.0);
    out_color = texel * v_color;
}
