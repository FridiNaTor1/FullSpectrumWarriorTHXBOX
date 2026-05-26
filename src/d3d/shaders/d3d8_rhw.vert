#version 450

layout(push_constant) uniform PushConstants {
    vec2 viewport;
    uint use_texture;
} pc;

layout(location = 0) in vec4 in_pos;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;

void main()
{
    float x = (in_pos.x / pc.viewport.x) * 2.0 - 1.0;
    float y = (in_pos.y / pc.viewport.y) * 2.0 - 1.0;
    gl_Position = vec4(x, y, in_pos.z, 1.0);
    v_color = in_color;
    v_uv = in_uv;
}
