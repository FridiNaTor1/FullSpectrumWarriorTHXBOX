#pragma once

#include <stdint.h>

typedef struct D3D8VulkanRhwVertex {
    float x, y, z, rhw;
    float r, g, b, a;
    float u, v;
} D3D8VulkanRhwVertex;

int  d3d8_vulkan_host_init(uint32_t width, uint32_t height);
void d3d8_vulkan_host_clear(uint32_t flags, uint32_t argb_color, float depth,
                             uint32_t width, uint32_t height);
int  d3d8_vulkan_host_draw_rhw(const D3D8VulkanRhwVertex *vertices,
                                uint32_t vertex_count,
                                const void *texture_bgra,
                                uint32_t texture_width,
                                uint32_t texture_height,
                                int depth_test,
                                int depth_write);
void d3d8_vulkan_host_present(void);
void d3d8_vulkan_host_present_bgra(const void *pixels, uint32_t width,
                                    uint32_t height, uint32_t pitch);
void d3d8_vulkan_host_set_window_title(const char *title);
void d3d8_vulkan_host_shutdown(void);
