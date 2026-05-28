/*
 * SDL/Vulkan presentation host for the Linux D3D8 ABI layer.
 *
 * It owns the window, Vulkan instance/device/swapchain, and uploads the
 * current D3D8 compatibility framebuffer into the swapchain. Native Vulkan
 * D3D8 draw lowering can be added behind this boundary incrementally.
 */

#include "d3d8_vulkan_host.h"

#include <SDL.h>
#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define MAX_SWAPCHAIN_IMAGES 8

typedef struct VulkanHost {
    SDL_Window *window;
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkSurfaceKHR surface;
    VkQueue graphics_queue;
    VkQueue present_queue;
    uint32_t graphics_family;
    uint32_t present_family;
    VkSwapchainKHR swapchain;
    VkFormat swapchain_format;
    VkExtent2D extent;
    uint32_t image_count;
    VkImage images[MAX_SWAPCHAIN_IMAGES];
    VkCommandPool command_pool;
    VkCommandBuffer command_buffers[MAX_SWAPCHAIN_IMAGES];
    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence in_flight;
    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;
    VkDeviceSize staging_size;
    uint8_t *scaled_frame;
    uint32_t scaled_width;
    uint32_t scaled_height;

    VkImage render_image;
    VkDeviceMemory render_memory;
    VkImageView render_view;
    VkImage depth_image;
    VkDeviceMemory depth_memory;
    VkImageView depth_view;
    VkFramebuffer render_framebuffer;
    VkImageLayout render_layout;
    VkImageLayout depth_layout;
    uint32_t render_width;
    uint32_t render_height;
    int gpu_frame_valid;

    VkRenderPass render_pass;
    VkDescriptorSetLayout descriptor_layout;
    VkPipelineLayout pipeline_layout;
    VkPipeline rhw_pipeline_nodepth;
    VkPipeline rhw_pipeline_depth_write;
    VkPipeline rhw_pipeline_depth_read;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;
    VkSampler sampler;

    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    VkDeviceSize vertex_size;

    VkImage texture_image;
    VkDeviceMemory texture_memory;
    VkImageView texture_view;
    VkImageLayout texture_layout;
    uint32_t texture_width;
    uint32_t texture_height;

    VkImage white_image;
    VkDeviceMemory white_memory;
    VkImageView white_view;
    VkImageLayout white_layout;

    int native_renderer_ready;
    int gpu_dump_checked;
    uint32_t gpu_dump_target_frame;
    uint32_t gpu_present_count;
    uint32_t rhw_draw_count;
    uint32_t rhw_textured_draw_count;
    uint32_t rhw_vertex_count;
    uint32_t present_gpu_count;
    uint32_t present_copy_count;
    uint32_t present_clear_count;
    int initialized;
} VulkanHost;

static VulkanHost g_vk;

static void destroy_swapchain(void)
{
    if (!g_vk.device) return;
    vkDeviceWaitIdle(g_vk.device);
    if (g_vk.command_pool && g_vk.image_count) {
        vkFreeCommandBuffers(g_vk.device, g_vk.command_pool, g_vk.image_count,
                             g_vk.command_buffers);
    }
    if (g_vk.swapchain) {
        vkDestroySwapchainKHR(g_vk.device, g_vk.swapchain, NULL);
    }
    memset(g_vk.images, 0, sizeof(g_vk.images));
    memset(g_vk.command_buffers, 0, sizeof(g_vk.command_buffers));
    g_vk.swapchain = VK_NULL_HANDLE;
    g_vk.image_count = 0;
}

static VkSurfaceFormatKHR choose_surface_format(const VkSurfaceFormatKHR *formats, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return formats[i];
        }
    }
    return formats[0];
}

static VkPresentModeKHR choose_present_mode(VkPhysicalDevice device)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, g_vk.surface, &count, NULL);
    VkPresentModeKHR *modes = (VkPresentModeKHR *)calloc(count ? count : 1, sizeof(*modes));
    if (!modes) return VK_PRESENT_MODE_FIFO_KHR;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, g_vk.surface, &count, modes);
    VkPresentModeKHR chosen = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < count; i++) {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            chosen = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        }
    }
    free(modes);
    return chosen;
}

static int create_swapchain(uint32_t requested_width, uint32_t requested_height)
{
    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vk.physical_device, g_vk.surface, &caps) != VK_SUCCESS) {
        return 0;
    }

    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        fprintf(stderr, "[D3D8/Vulkan] Surface does not support transfer clears\n");
        return 0;
    }

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vk.physical_device, g_vk.surface, &format_count, NULL);
    if (!format_count) return 0;

    VkSurfaceFormatKHR *formats = (VkSurfaceFormatKHR *)calloc(format_count, sizeof(*formats));
    if (!formats) return 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vk.physical_device, g_vk.surface, &format_count, formats);
    VkSurfaceFormatKHR surface_format = choose_surface_format(formats, format_count);
    free(formats);

    VkExtent2D extent;
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        extent.width = requested_width ? requested_width : 640;
        extent.height = requested_height ? requested_height : 480;
        if (extent.width < caps.minImageExtent.width) extent.width = caps.minImageExtent.width;
        if (extent.height < caps.minImageExtent.height) extent.height = caps.minImageExtent.height;
        if (extent.width > caps.maxImageExtent.width) extent.width = caps.maxImageExtent.width;
        if (extent.height > caps.maxImageExtent.height) extent.height = caps.maxImageExtent.height;
    }

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount && image_count > caps.maxImageCount) image_count = caps.maxImageCount;
    if (image_count > MAX_SWAPCHAIN_IMAGES) image_count = MAX_SWAPCHAIN_IMAGES;

    uint32_t queue_indices[2] = { g_vk.graphics_family, g_vk.present_family };
    VkSwapchainCreateInfoKHR info;
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = g_vk.surface;
    info.minImageCount = image_count;
    info.imageFormat = surface_format.format;
    info.imageColorSpace = surface_format.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (g_vk.graphics_family != g_vk.present_family) {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = queue_indices;
    } else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = choose_present_mode(g_vk.physical_device);
    info.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(g_vk.device, &info, NULL, &g_vk.swapchain) != VK_SUCCESS) {
        return 0;
    }

    g_vk.swapchain_format = surface_format.format;
    g_vk.extent = extent;
    vkGetSwapchainImagesKHR(g_vk.device, g_vk.swapchain, &g_vk.image_count, NULL);
    if (g_vk.image_count > MAX_SWAPCHAIN_IMAGES) g_vk.image_count = MAX_SWAPCHAIN_IMAGES;
    vkGetSwapchainImagesKHR(g_vk.device, g_vk.swapchain, &g_vk.image_count, g_vk.images);

    VkCommandBufferAllocateInfo alloc;
    memset(&alloc, 0, sizeof(alloc));
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = g_vk.command_pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = g_vk.image_count;
    return vkAllocateCommandBuffers(g_vk.device, &alloc, g_vk.command_buffers) == VK_SUCCESS;
}

static int recreate_swapchain(void)
{
    if (!g_vk.initialized || !g_vk.device) return 0;

    int w = 0, h = 0;
    SDL_Vulkan_GetDrawableSize(g_vk.window, &w, &h);
    if (w <= 0 || h <= 0) return 0;

    vkDeviceWaitIdle(g_vk.device);
    destroy_swapchain();
    if (!create_swapchain((uint32_t)w, (uint32_t)h)) {
        fprintf(stderr, "[D3D8/Vulkan] Swapchain recreation failed\n");
        return 0;
    }
    fprintf(stderr, "[D3D8/Vulkan] Swapchain resized (%ux%u)\n",
            g_vk.extent.width, g_vk.extent.height);
    return 1;
}

static int pick_device(void)
{
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(g_vk.instance, &device_count, NULL);
    if (!device_count) return 0;

    VkPhysicalDevice *devices = (VkPhysicalDevice *)calloc(device_count, sizeof(*devices));
    if (!devices) return 0;
    vkEnumeratePhysicalDevices(g_vk.instance, &device_count, devices);

    for (uint32_t d = 0; d < device_count; d++) {
        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &family_count, NULL);
        VkQueueFamilyProperties *families =
            (VkQueueFamilyProperties *)calloc(family_count, sizeof(*families));
        if (!families) continue;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &family_count, families);

        uint32_t graphics = UINT32_MAX;
        uint32_t present = UINT32_MAX;
        for (uint32_t i = 0; i < family_count; i++) {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) graphics = i;
            VkBool32 supports_present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(devices[d], i, g_vk.surface, &supports_present);
            if (supports_present) present = i;
        }
        free(families);

        if (graphics != UINT32_MAX && present != UINT32_MAX) {
            g_vk.physical_device = devices[d];
            g_vk.graphics_family = graphics;
            g_vk.present_family = present;
            free(devices);
            return 1;
        }
    }

    free(devices);
    return 0;
}

static int create_device(void)
{
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queues[2];
    memset(queues, 0, sizeof(queues));
    queues[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queues[0].queueFamilyIndex = g_vk.graphics_family;
    queues[0].queueCount = 1;
    queues[0].pQueuePriorities = &priority;
    uint32_t queue_count = 1;

    if (g_vk.present_family != g_vk.graphics_family) {
        queues[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queues[1].queueFamilyIndex = g_vk.present_family;
        queues[1].queueCount = 1;
        queues[1].pQueuePriorities = &priority;
        queue_count = 2;
    }

    const char *extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = queue_count;
    info.pQueueCreateInfos = queues;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = extensions;

    if (vkCreateDevice(g_vk.physical_device, &info, NULL, &g_vk.device) != VK_SUCCESS) {
        return 0;
    }

    vkGetDeviceQueue(g_vk.device, g_vk.graphics_family, 0, &g_vk.graphics_queue);
    vkGetDeviceQueue(g_vk.device, g_vk.present_family, 0, &g_vk.present_queue);

    VkCommandPoolCreateInfo pool;
    memset(&pool, 0, sizeof(pool));
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = g_vk.graphics_family;
    if (vkCreateCommandPool(g_vk.device, &pool, NULL, &g_vk.command_pool) != VK_SUCCESS) {
        return 0;
    }

    VkSemaphoreCreateInfo sem;
    memset(&sem, 0, sizeof(sem));
    sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fence;
    memset(&fence, 0, sizeof(fence));
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    return vkCreateSemaphore(g_vk.device, &sem, NULL, &g_vk.image_available) == VK_SUCCESS &&
           vkCreateSemaphore(g_vk.device, &sem, NULL, &g_vk.render_finished) == VK_SUCCESS &&
           vkCreateFence(g_vk.device, &fence, NULL, &g_vk.in_flight) == VK_SUCCESS;
}

static uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(g_vk.physical_device, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) &&
            ((mem_props.memoryTypes[i].propertyFlags & props) == props)) {
            return i;
        }
    }
    return UINT32_MAX;
}

static int read_file_bytes(const char *path, uint8_t **out_data, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return 0;
    }
    uint8_t *data = (uint8_t *)malloc((size_t)size);
    if (!data) {
        fclose(f);
        return 0;
    }
    if (fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return 0;
    }
    fclose(f);
    *out_data = data;
    *out_size = (size_t)size;
    return 1;
}

static VkShaderModule load_shader_module(const char *name)
{
#ifndef XBOXRECOMP_VK_SHADER_DIR
#define XBOXRECOMP_VK_SHADER_DIR "."
#endif
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", XBOXRECOMP_VK_SHADER_DIR, name);
    uint8_t *bytes = NULL;
    size_t size = 0;
    if (!read_file_bytes(path, &bytes, &size)) {
        fprintf(stderr, "[D3D8/Vulkan] Could not read shader: %s\n", path);
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode = (const uint32_t *)(const void *)bytes;
    VkShaderModule module = VK_NULL_HANDLE;
    VkResult r = vkCreateShaderModule(g_vk.device, &info, NULL, &module);
    free(bytes);
    if (r != VK_SUCCESS) {
        fprintf(stderr, "[D3D8/Vulkan] vkCreateShaderModule failed for %s (%d)\n", name, r);
        return VK_NULL_HANDLE;
    }
    return module;
}

static int create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags props,
                         VkBuffer *buffer, VkDeviceMemory *memory)
{
    VkBufferCreateInfo buf;
    memset(&buf, 0, sizeof(buf));
    buf.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf.size = size;
    buf.usage = usage;
    buf.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(g_vk.device, &buf, NULL, buffer) != VK_SUCCESS) {
        return 0;
    }

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_vk.device, *buffer, &req);
    uint32_t mem_type = find_memory_type(req.memoryTypeBits, props);
    if (mem_type == UINT32_MAX) return 0;

    VkMemoryAllocateInfo alloc;
    memset(&alloc, 0, sizeof(alloc));
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = mem_type;
    if (vkAllocateMemory(g_vk.device, &alloc, NULL, memory) != VK_SUCCESS) {
        return 0;
    }
    return vkBindBufferMemory(g_vk.device, *buffer, *memory, 0) == VK_SUCCESS;
}

static int create_image(uint32_t width, uint32_t height, VkFormat format,
                        VkImageUsageFlags usage, VkImage *image,
                        VkDeviceMemory *memory)
{
    VkImageCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent.width = width;
    info.extent.height = height;
    info.extent.depth = 1;
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(g_vk.device, &info, NULL, image) != VK_SUCCESS) {
        return 0;
    }

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(g_vk.device, *image, &req);
    uint32_t mem_type = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mem_type == UINT32_MAX) return 0;

    VkMemoryAllocateInfo alloc;
    memset(&alloc, 0, sizeof(alloc));
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = mem_type;
    if (vkAllocateMemory(g_vk.device, &alloc, NULL, memory) != VK_SUCCESS) {
        return 0;
    }
    return vkBindImageMemory(g_vk.device, *image, *memory, 0) == VK_SUCCESS;
}

static VkImageView create_image_view_aspect(VkImage image, VkFormat format,
                                            VkImageAspectFlags aspect)
{
    VkImageViewCreateInfo view;
    memset(&view, 0, sizeof(view));
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange.aspectMask = aspect;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    VkImageView out = VK_NULL_HANDLE;
    if (vkCreateImageView(g_vk.device, &view, NULL, &out) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return out;
}

static VkImageView create_image_view(VkImage image, VkFormat format)
{
    return create_image_view_aspect(image, format, VK_IMAGE_ASPECT_COLOR_BIT);
}

static VkCommandBuffer begin_one_time_commands(void)
{
    VkCommandBufferAllocateInfo alloc;
    memset(&alloc, 0, sizeof(alloc));
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = g_vk.command_pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(g_vk.device, &alloc, &cmd) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo begin;
    memset(&begin, 0, sizeof(begin));
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    return cmd;
}

static void end_one_time_commands(VkCommandBuffer cmd)
{
    if (!cmd) return;
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit;
    memset(&submit, 0, sizeof(submit));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(g_vk.graphics_queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(g_vk.graphics_queue);
    vkFreeCommandBuffers(g_vk.device, g_vk.command_pool, 1, &cmd);
}

static void image_barrier_aspect(VkCommandBuffer cmd, VkImage image,
                                 VkImageLayout old_layout, VkImageLayout new_layout,
                                 VkAccessFlags src_access, VkAccessFlags dst_access,
                                 VkPipelineStageFlags src_stage,
                                 VkPipelineStageFlags dst_stage,
                                 VkImageAspectFlags aspect)
{
    VkImageMemoryBarrier b;
    memset(&b, 0, sizeof(b));
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = src_access;
    b.dstAccessMask = dst_access;
    b.oldLayout = old_layout;
    b.newLayout = new_layout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = aspect;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &b);
}

static void image_barrier(VkCommandBuffer cmd, VkImage image,
                          VkImageLayout old_layout, VkImageLayout new_layout,
                          VkAccessFlags src_access, VkAccessFlags dst_access,
                          VkPipelineStageFlags src_stage,
                          VkPipelineStageFlags dst_stage)
{
    image_barrier_aspect(cmd, image, old_layout, new_layout,
                         src_access, dst_access, src_stage, dst_stage,
                         VK_IMAGE_ASPECT_COLOR_BIT);
}

static int ensure_staging_buffer(VkDeviceSize size)
{
    if (g_vk.staging_buffer && g_vk.staging_size >= size) return 1;

    if (g_vk.staging_buffer) {
        vkDestroyBuffer(g_vk.device, g_vk.staging_buffer, NULL);
        g_vk.staging_buffer = VK_NULL_HANDLE;
    }
    if (g_vk.staging_memory) {
        vkFreeMemory(g_vk.device, g_vk.staging_memory, NULL);
        g_vk.staging_memory = VK_NULL_HANDLE;
    }

    VkBufferCreateInfo buf;
    memset(&buf, 0, sizeof(buf));
    buf.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf.size = size;
    buf.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buf.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(g_vk.device, &buf, NULL, &g_vk.staging_buffer) != VK_SUCCESS) {
        return 0;
    }

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_vk.device, g_vk.staging_buffer, &req);
    uint32_t mem_type = find_memory_type(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mem_type == UINT32_MAX) return 0;

    VkMemoryAllocateInfo alloc;
    memset(&alloc, 0, sizeof(alloc));
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = mem_type;
    if (vkAllocateMemory(g_vk.device, &alloc, NULL, &g_vk.staging_memory) != VK_SUCCESS) {
        return 0;
    }
    if (vkBindBufferMemory(g_vk.device, g_vk.staging_buffer, g_vk.staging_memory, 0) != VK_SUCCESS) {
        return 0;
    }
    g_vk.staging_size = size;
    return 1;
}

static const void *prepare_present_pixels(const void *pixels, uint32_t width,
                                          uint32_t height, uint32_t pitch,
                                          uint32_t *out_width,
                                          uint32_t *out_height,
                                          uint32_t *out_pitch)
{
    if (!pixels || !width || !height || !pitch ||
        !g_vk.extent.width || !g_vk.extent.height) {
        return NULL;
    }

    uint32_t dst_w = g_vk.extent.width;
    uint32_t dst_h = g_vk.extent.height;
    size_t dst_size = (size_t)dst_w * dst_h * 4;
    if (!g_vk.scaled_frame ||
        g_vk.scaled_width != dst_w ||
        g_vk.scaled_height != dst_h) {
        free(g_vk.scaled_frame);
        g_vk.scaled_frame = (uint8_t *)malloc(dst_size);
        if (!g_vk.scaled_frame) {
            g_vk.scaled_width = 0;
            g_vk.scaled_height = 0;
            return NULL;
        }
        g_vk.scaled_width = dst_w;
        g_vk.scaled_height = dst_h;
    }

    memset(g_vk.scaled_frame, 0, dst_size);

    uint64_t scale_x_num = dst_w;
    uint64_t scale_x_den = width;
    uint64_t scale_y_num = dst_h;
    uint64_t scale_y_den = height;
    uint64_t scale_num = scale_x_num * scale_y_den <= scale_y_num * scale_x_den ?
                         scale_x_num : scale_y_num;
    uint64_t scale_den = scale_x_num * scale_y_den <= scale_y_num * scale_x_den ?
                         scale_x_den : scale_y_den;

    uint32_t fit_w = (uint32_t)(((uint64_t)width * scale_num) / scale_den);
    uint32_t fit_h = (uint32_t)(((uint64_t)height * scale_num) / scale_den);
    if (!fit_w) fit_w = 1;
    if (!fit_h) fit_h = 1;
    if (fit_w > dst_w) fit_w = dst_w;
    if (fit_h > dst_h) fit_h = dst_h;

    uint32_t off_x = (dst_w - fit_w) / 2;
    uint32_t off_y = (dst_h - fit_h) / 2;
    const uint8_t *src_base = (const uint8_t *)pixels;
    for (uint32_t y = 0; y < fit_h; y++) {
        uint32_t src_y = (uint32_t)(((uint64_t)y * height) / fit_h);
        const uint8_t *src_row = src_base + (size_t)src_y * pitch;
        uint8_t *dst_row = g_vk.scaled_frame + ((size_t)(off_y + y) * dst_w + off_x) * 4;
        for (uint32_t x = 0; x < fit_w; x++) {
            uint32_t src_x = (uint32_t)(((uint64_t)x * width) / fit_w);
            memcpy(dst_row + (size_t)x * 4, src_row + (size_t)src_x * 4, 4);
        }
    }

    *out_width = dst_w;
    *out_height = dst_h;
    *out_pitch = dst_w * 4;
    return g_vk.scaled_frame;
}

static void destroy_native_texture(void)
{
    if (g_vk.texture_view) vkDestroyImageView(g_vk.device, g_vk.texture_view, NULL);
    if (g_vk.texture_image) vkDestroyImage(g_vk.device, g_vk.texture_image, NULL);
    if (g_vk.texture_memory) vkFreeMemory(g_vk.device, g_vk.texture_memory, NULL);
    g_vk.texture_view = VK_NULL_HANDLE;
    g_vk.texture_image = VK_NULL_HANDLE;
    g_vk.texture_memory = VK_NULL_HANDLE;
    g_vk.texture_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    g_vk.texture_width = 0;
    g_vk.texture_height = 0;
}

static void destroy_render_target(void)
{
    if (g_vk.render_framebuffer) vkDestroyFramebuffer(g_vk.device, g_vk.render_framebuffer, NULL);
    if (g_vk.render_view) vkDestroyImageView(g_vk.device, g_vk.render_view, NULL);
    if (g_vk.render_image) vkDestroyImage(g_vk.device, g_vk.render_image, NULL);
    if (g_vk.render_memory) vkFreeMemory(g_vk.device, g_vk.render_memory, NULL);
    if (g_vk.depth_view) vkDestroyImageView(g_vk.device, g_vk.depth_view, NULL);
    if (g_vk.depth_image) vkDestroyImage(g_vk.device, g_vk.depth_image, NULL);
    if (g_vk.depth_memory) vkFreeMemory(g_vk.device, g_vk.depth_memory, NULL);
    g_vk.render_framebuffer = VK_NULL_HANDLE;
    g_vk.render_view = VK_NULL_HANDLE;
    g_vk.render_image = VK_NULL_HANDLE;
    g_vk.render_memory = VK_NULL_HANDLE;
    g_vk.depth_view = VK_NULL_HANDLE;
    g_vk.depth_image = VK_NULL_HANDLE;
    g_vk.depth_memory = VK_NULL_HANDLE;
    g_vk.render_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    g_vk.depth_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    g_vk.render_width = 0;
    g_vk.render_height = 0;
    g_vk.gpu_frame_valid = 0;
}

static void destroy_native_renderer(void)
{
    if (!g_vk.device) return;
    destroy_render_target();
    destroy_native_texture();
    if (g_vk.white_view) vkDestroyImageView(g_vk.device, g_vk.white_view, NULL);
    if (g_vk.white_image) vkDestroyImage(g_vk.device, g_vk.white_image, NULL);
    if (g_vk.white_memory) vkFreeMemory(g_vk.device, g_vk.white_memory, NULL);
    if (g_vk.vertex_buffer) vkDestroyBuffer(g_vk.device, g_vk.vertex_buffer, NULL);
    if (g_vk.vertex_memory) vkFreeMemory(g_vk.device, g_vk.vertex_memory, NULL);
    if (g_vk.sampler) vkDestroySampler(g_vk.device, g_vk.sampler, NULL);
    if (g_vk.descriptor_pool) vkDestroyDescriptorPool(g_vk.device, g_vk.descriptor_pool, NULL);
    if (g_vk.rhw_pipeline_nodepth) vkDestroyPipeline(g_vk.device, g_vk.rhw_pipeline_nodepth, NULL);
    if (g_vk.rhw_pipeline_depth_write) vkDestroyPipeline(g_vk.device, g_vk.rhw_pipeline_depth_write, NULL);
    if (g_vk.rhw_pipeline_depth_read) vkDestroyPipeline(g_vk.device, g_vk.rhw_pipeline_depth_read, NULL);
    if (g_vk.pipeline_layout) vkDestroyPipelineLayout(g_vk.device, g_vk.pipeline_layout, NULL);
    if (g_vk.descriptor_layout) vkDestroyDescriptorSetLayout(g_vk.device, g_vk.descriptor_layout, NULL);
    if (g_vk.render_pass) vkDestroyRenderPass(g_vk.device, g_vk.render_pass, NULL);
    g_vk.white_view = VK_NULL_HANDLE;
    g_vk.white_image = VK_NULL_HANDLE;
    g_vk.white_memory = VK_NULL_HANDLE;
    g_vk.white_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    g_vk.vertex_buffer = VK_NULL_HANDLE;
    g_vk.vertex_memory = VK_NULL_HANDLE;
    g_vk.vertex_size = 0;
    g_vk.sampler = VK_NULL_HANDLE;
    g_vk.descriptor_pool = VK_NULL_HANDLE;
    g_vk.descriptor_set = VK_NULL_HANDLE;
    g_vk.rhw_pipeline_nodepth = VK_NULL_HANDLE;
    g_vk.rhw_pipeline_depth_write = VK_NULL_HANDLE;
    g_vk.rhw_pipeline_depth_read = VK_NULL_HANDLE;
    g_vk.pipeline_layout = VK_NULL_HANDLE;
    g_vk.descriptor_layout = VK_NULL_HANDLE;
    g_vk.render_pass = VK_NULL_HANDLE;
    g_vk.native_renderer_ready = 0;
}

static int upload_image_bgra(VkImage image, VkImageLayout *layout,
                             uint32_t width, uint32_t height, const void *pixels)
{
    VkDeviceSize size = (VkDeviceSize)width * height * 4;
    if (!ensure_staging_buffer(size)) return 0;

    void *mapped = NULL;
    if (vkMapMemory(g_vk.device, g_vk.staging_memory, 0, size, 0, &mapped) != VK_SUCCESS) {
        return 0;
    }
    memcpy(mapped, pixels, (size_t)size);
    vkUnmapMemory(g_vk.device, g_vk.staging_memory);

    VkCommandBuffer cmd = begin_one_time_commands();
    if (!cmd) return 0;
    image_barrier(cmd, image, *layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  0, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy copy;
    memset(&copy, 0, sizeof(copy));
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = width;
    copy.imageExtent.height = height;
    copy.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(cmd, g_vk.staging_buffer, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    image_barrier(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    end_one_time_commands(cmd);
    *layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return 1;
}

static int ensure_white_texture(void)
{
    if (g_vk.white_image && g_vk.white_view) return 1;
    if (!create_image(1, 1, VK_FORMAT_B8G8R8A8_UNORM,
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                      &g_vk.white_image, &g_vk.white_memory)) {
        return 0;
    }
    g_vk.white_view = create_image_view(g_vk.white_image, VK_FORMAT_B8G8R8A8_UNORM);
    if (!g_vk.white_view) return 0;
    uint32_t white = 0xFFFFFFFFu;
    g_vk.white_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return upload_image_bgra(g_vk.white_image, &g_vk.white_layout, 1, 1, &white);
}

static int ensure_native_texture(uint32_t width, uint32_t height, const void *pixels)
{
    if (!pixels || !width || !height) return ensure_white_texture();
    if (!g_vk.texture_image || g_vk.texture_width != width || g_vk.texture_height != height) {
        destroy_native_texture();
        if (!create_image(width, height, VK_FORMAT_B8G8R8A8_UNORM,
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          &g_vk.texture_image, &g_vk.texture_memory)) {
            return 0;
        }
        g_vk.texture_view = create_image_view(g_vk.texture_image, VK_FORMAT_B8G8R8A8_UNORM);
        if (!g_vk.texture_view) return 0;
        g_vk.texture_width = width;
        g_vk.texture_height = height;
        g_vk.texture_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    return upload_image_bgra(g_vk.texture_image, &g_vk.texture_layout, width, height, pixels);
}

static int ensure_vertex_buffer(VkDeviceSize size)
{
    if (g_vk.vertex_buffer && g_vk.vertex_size >= size) return 1;
    if (g_vk.vertex_buffer) {
        vkDestroyBuffer(g_vk.device, g_vk.vertex_buffer, NULL);
        g_vk.vertex_buffer = VK_NULL_HANDLE;
    }
    if (g_vk.vertex_memory) {
        vkFreeMemory(g_vk.device, g_vk.vertex_memory, NULL);
        g_vk.vertex_memory = VK_NULL_HANDLE;
    }
    if (!create_buffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &g_vk.vertex_buffer, &g_vk.vertex_memory)) {
        return 0;
    }
    g_vk.vertex_size = size;
    return 1;
}

static int create_rhw_pipeline(VkShaderModule vs, VkShaderModule fs,
                               int depth_test, int depth_write,
                               VkPipeline *out_pipeline)
{
    VkPipelineShaderStageCreateInfo stages[2];
    memset(stages, 0, sizeof(stages));
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    VkVertexInputBindingDescription vb;
    memset(&vb, 0, sizeof(vb));
    vb.binding = 0;
    vb.stride = sizeof(D3D8VulkanRhwVertex);
    vb.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3];
    memset(attrs, 0, sizeof(attrs));
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[0].offset = offsetof(D3D8VulkanRhwVertex, x);
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[1].offset = offsetof(D3D8VulkanRhwVertex, r);
    attrs[2].location = 2;
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = offsetof(D3D8VulkanRhwVertex, u);

    VkPipelineVertexInputStateCreateInfo vi;
    memset(&vi, 0, sizeof(vi));
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vb;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia;
    memset(&ia, 0, sizeof(ia));
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp;
    memset(&vp, 0, sizeof(vp));
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs;
    memset(&rs, 0, sizeof(rs));
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms;
    memset(&ms, 0, sizeof(ms));
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds;
    memset(&ds, 0, sizeof(ds));
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = depth_test ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    ds.minDepthBounds = 0.0f;
    ds.maxDepthBounds = 1.0f;

    VkPipelineColorBlendAttachmentState blend;
    memset(&blend, 0, sizeof(blend));
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb;
    memset(&cb, 0, sizeof(cb));
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;

    VkDynamicState dyns[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn;
    memset(&dyn, 0, sizeof(dyn));
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyns;

    VkGraphicsPipelineCreateInfo gp;
    memset(&gp, 0, sizeof(gp));
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pDepthStencilState = &ds;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &dyn;
    gp.layout = g_vk.pipeline_layout;
    gp.renderPass = g_vk.render_pass;
    return vkCreateGraphicsPipelines(g_vk.device, VK_NULL_HANDLE, 1, &gp, NULL,
                                     out_pipeline) == VK_SUCCESS;
}

static int create_native_renderer_objects(void)
{
    if (g_vk.native_renderer_ready) return 1;

    VkAttachmentDescription attachments[2];
    memset(attachments, 0, sizeof(attachments));
    attachments[0].format = VK_FORMAT_B8G8R8A8_UNORM;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref;
    memset(&color_ref, 0, sizeof(color_ref));
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depth_ref;
    memset(&depth_ref, 0, sizeof(depth_ref));
    depth_ref.attachment = 1;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass;
    memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkRenderPassCreateInfo rp;
    memset(&rp, 0, sizeof(rp));
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 2;
    rp.pAttachments = attachments;
    rp.subpassCount = 1;
    rp.pSubpasses = &subpass;
    if (vkCreateRenderPass(g_vk.device, &rp, NULL, &g_vk.render_pass) != VK_SUCCESS) {
        return 0;
    }

    VkDescriptorSetLayoutBinding binding;
    memset(&binding, 0, sizeof(binding));
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dsl;
    memset(&dsl, 0, sizeof(dsl));
    dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = 1;
    dsl.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(g_vk.device, &dsl, NULL, &g_vk.descriptor_layout) != VK_SUCCESS) {
        return 0;
    }

    VkPushConstantRange push;
    memset(&push, 0, sizeof(push));
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push.offset = 0;
    push.size = 16;

    VkPipelineLayoutCreateInfo pl;
    memset(&pl, 0, sizeof(pl));
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &g_vk.descriptor_layout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(g_vk.device, &pl, NULL, &g_vk.pipeline_layout) != VK_SUCCESS) {
        return 0;
    }

    VkShaderModule vs = load_shader_module("d3d8_rhw.vert.spv");
    VkShaderModule fs = load_shader_module("d3d8_rhw.frag.spv");
    if (!vs || !fs) return 0;

    if (!create_rhw_pipeline(vs, fs, 0, 0, &g_vk.rhw_pipeline_nodepth) ||
        !create_rhw_pipeline(vs, fs, 1, 1, &g_vk.rhw_pipeline_depth_write) ||
        !create_rhw_pipeline(vs, fs, 1, 0, &g_vk.rhw_pipeline_depth_read)) {
        vkDestroyShaderModule(g_vk.device, vs, NULL);
        vkDestroyShaderModule(g_vk.device, fs, NULL);
        return 0;
    }
    vkDestroyShaderModule(g_vk.device, vs, NULL);
    vkDestroyShaderModule(g_vk.device, fs, NULL);

    VkSamplerCreateInfo sampler;
    memset(&sampler, 0, sizeof(sampler));
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = 1.0f;
    if (vkCreateSampler(g_vk.device, &sampler, NULL, &g_vk.sampler) != VK_SUCCESS) {
        return 0;
    }

    VkDescriptorPoolSize pool_size;
    memset(&pool_size, 0, sizeof(pool_size));
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1;
    VkDescriptorPoolCreateInfo pool;
    memset(&pool, 0, sizeof(pool));
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 1;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &pool_size;
    if (vkCreateDescriptorPool(g_vk.device, &pool, NULL, &g_vk.descriptor_pool) != VK_SUCCESS) {
        return 0;
    }

    VkDescriptorSetAllocateInfo ds_alloc;
    memset(&ds_alloc, 0, sizeof(ds_alloc));
    ds_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ds_alloc.descriptorPool = g_vk.descriptor_pool;
    ds_alloc.descriptorSetCount = 1;
    ds_alloc.pSetLayouts = &g_vk.descriptor_layout;
    if (vkAllocateDescriptorSets(g_vk.device, &ds_alloc, &g_vk.descriptor_set) != VK_SUCCESS) {
        return 0;
    }

    if (!ensure_white_texture()) return 0;
    g_vk.native_renderer_ready = 1;
    fprintf(stderr, "[D3D8/Vulkan] Native RHW draw path initialized\n");
    return 1;
}

static int ensure_render_target(uint32_t width, uint32_t height)
{
    if (!width) width = 640;
    if (!height) height = 480;
    if (!create_native_renderer_objects()) return 0;
    if (g_vk.render_image && g_vk.render_width == width && g_vk.render_height == height) {
        return 1;
    }

    destroy_render_target();
    if (!create_image(width, height, VK_FORMAT_B8G8R8A8_UNORM,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                      &g_vk.render_image, &g_vk.render_memory)) {
        return 0;
    }
    g_vk.render_view = create_image_view(g_vk.render_image, VK_FORMAT_B8G8R8A8_UNORM);
    if (!g_vk.render_view) return 0;
    if (!create_image(width, height, VK_FORMAT_D32_SFLOAT,
                      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                      &g_vk.depth_image, &g_vk.depth_memory)) {
        return 0;
    }
    g_vk.depth_view = create_image_view_aspect(g_vk.depth_image,
                                               VK_FORMAT_D32_SFLOAT,
                                               VK_IMAGE_ASPECT_DEPTH_BIT);
    if (!g_vk.depth_view) return 0;

    VkImageView attachments[2] = { g_vk.render_view, g_vk.depth_view };
    VkFramebufferCreateInfo fb;
    memset(&fb, 0, sizeof(fb));
    fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb.renderPass = g_vk.render_pass;
    fb.attachmentCount = 2;
    fb.pAttachments = attachments;
    fb.width = width;
    fb.height = height;
    fb.layers = 1;
    if (vkCreateFramebuffer(g_vk.device, &fb, NULL, &g_vk.render_framebuffer) != VK_SUCCESS) {
        return 0;
    }
    g_vk.render_width = width;
    g_vk.render_height = height;
    g_vk.render_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    g_vk.depth_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    d3d8_vulkan_host_clear(1 | 2, 0xFF05070Au, 1.0f, width, height);
    return 1;
}

void d3d8_vulkan_host_clear(uint32_t flags, uint32_t argb_color, float depth,
                            uint32_t width, uint32_t height)
{
    if (!g_vk.initialized || !(flags & (1 | 2))) return;
    if (!ensure_render_target(width, height)) return;

    VkCommandBuffer cmd = begin_one_time_commands();
    if (!cmd) return;
    if (flags & 1) {
        image_barrier(cmd, g_vk.render_image, g_vk.render_layout,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      0, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkClearColorValue color;
        color.float32[0] = ((argb_color >> 16) & 255) / 255.0f;
        color.float32[1] = ((argb_color >> 8) & 255) / 255.0f;
        color.float32[2] = (argb_color & 255) / 255.0f;
        color.float32[3] = ((argb_color >> 24) & 255) / 255.0f;
        VkImageSubresourceRange range;
        memset(&range, 0, sizeof(range));
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = 1;
        vkCmdClearColorImage(cmd, g_vk.render_image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &color, 1, &range);
        image_barrier(cmd, g_vk.render_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        g_vk.render_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    if (flags & 2) {
        image_barrier_aspect(cmd, g_vk.depth_image, g_vk.depth_layout,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             0, VK_ACCESS_TRANSFER_WRITE_BIT,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_IMAGE_ASPECT_DEPTH_BIT);
        VkClearDepthStencilValue depth_value;
        memset(&depth_value, 0, sizeof(depth_value));
        depth_value.depth = depth;
        VkImageSubresourceRange range;
        memset(&range, 0, sizeof(range));
        range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        range.levelCount = 1;
        range.layerCount = 1;
        vkCmdClearDepthStencilImage(cmd, g_vk.depth_image,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    &depth_value, 1, &range);
        image_barrier_aspect(cmd, g_vk.depth_image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                             VK_ACCESS_TRANSFER_WRITE_BIT,
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                             VK_IMAGE_ASPECT_DEPTH_BIT);
        g_vk.depth_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    end_one_time_commands(cmd);
    g_vk.gpu_frame_valid = 1;
}

int d3d8_vulkan_host_draw_rhw(const D3D8VulkanRhwVertex *vertices,
                              uint32_t vertex_count,
                              const void *texture_bgra,
                              uint32_t texture_width,
                              uint32_t texture_height,
                              int depth_test,
                              int depth_write)
{
    if (!g_vk.initialized || !vertices || vertex_count == 0) return 0;
    if (!ensure_render_target(g_vk.render_width ? g_vk.render_width : 640,
                              g_vk.render_height ? g_vk.render_height : 480)) {
        return 0;
    }

    VkDeviceSize vertex_bytes = (VkDeviceSize)vertex_count * sizeof(*vertices);
    if (!ensure_vertex_buffer(vertex_bytes)) return 0;
    void *mapped = NULL;
    if (vkMapMemory(g_vk.device, g_vk.vertex_memory, 0, vertex_bytes, 0, &mapped) != VK_SUCCESS) {
        return 0;
    }
    memcpy(mapped, vertices, (size_t)vertex_bytes);
    vkUnmapMemory(g_vk.device, g_vk.vertex_memory);

    int use_texture = texture_bgra && texture_width && texture_height;
    if (use_texture) {
        if (!ensure_native_texture(texture_width, texture_height, texture_bgra)) return 0;
    } else if (!ensure_white_texture()) {
        return 0;
    }

    VkImageView image_view = use_texture ? g_vk.texture_view : g_vk.white_view;
    VkDescriptorImageInfo image_info;
    memset(&image_info, 0, sizeof(image_info));
    image_info.sampler = g_vk.sampler;
    image_info.imageView = image_view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write;
    memset(&write, 0, sizeof(write));
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = g_vk.descriptor_set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_info;
    vkUpdateDescriptorSets(g_vk.device, 1, &write, 0, NULL);

    VkCommandBuffer cmd = begin_one_time_commands();
    if (!cmd) return 0;
    if (g_vk.render_layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        image_barrier(cmd, g_vk.render_image, g_vk.render_layout,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        g_vk.render_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    if (g_vk.depth_layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        image_barrier_aspect(cmd, g_vk.depth_image, g_vk.depth_layout,
                             VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                             0,
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                             VK_IMAGE_ASPECT_DEPTH_BIT);
        g_vk.depth_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    VkRenderPassBeginInfo rp_begin;
    memset(&rp_begin, 0, sizeof(rp_begin));
    rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass = g_vk.render_pass;
    rp_begin.framebuffer = g_vk.render_framebuffer;
    rp_begin.renderArea.extent.width = g_vk.render_width;
    rp_begin.renderArea.extent.height = g_vk.render_height;
    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport;
    memset(&viewport, 0, sizeof(viewport));
    viewport.width = (float)g_vk.render_width;
    viewport.height = (float)g_vk.render_height;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor;
    memset(&scissor, 0, sizeof(scissor));
    scissor.extent.width = g_vk.render_width;
    scissor.extent.height = g_vk.render_height;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    VkPipeline pipeline = g_vk.rhw_pipeline_nodepth;
    if (depth_test) {
        pipeline = depth_write ? g_vk.rhw_pipeline_depth_write :
                                 g_vk.rhw_pipeline_depth_read;
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &g_vk.vertex_buffer, &offset);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            g_vk.pipeline_layout, 0, 1, &g_vk.descriptor_set,
                            0, NULL);
    struct {
        float viewport[2];
        uint32_t use_texture;
        uint32_t pad;
    } pc;
    pc.viewport[0] = (float)g_vk.render_width;
    pc.viewport[1] = (float)g_vk.render_height;
    pc.use_texture = use_texture ? 1u : 0u;
    pc.pad = 0;
    vkCmdPushConstants(cmd, g_vk.pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDraw(cmd, vertex_count, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    end_one_time_commands(cmd);
    g_vk.render_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    g_vk.gpu_frame_valid = 1;

    g_vk.rhw_draw_count++;
    g_vk.rhw_vertex_count += vertex_count;
    if (use_texture) {
        g_vk.rhw_textured_draw_count++;
    }
    if (g_vk.rhw_draw_count <= 8 || (g_vk.rhw_draw_count % 512) == 0) {
        fprintf(stderr,
                "[D3D8/Vulkan] native RHW draw #%u vertices=%u textured=%u tex=%ux%u depth_test=%d depth_write=%d total_vertices=%u\n",
                g_vk.rhw_draw_count,
                vertex_count,
                use_texture ? 1u : 0u,
                texture_width,
                texture_height,
                depth_test,
                depth_write,
                g_vk.rhw_vertex_count);
    }
    return 1;
}

static void maybe_dump_gpu_frame(void)
{
    if (!g_vk.gpu_dump_checked) {
        const char *dump = getenv("XBOXRECOMP_DUMP_VULKAN_FRAME");
        g_vk.gpu_dump_target_frame = (dump && dump[0]) ? (uint32_t)strtoul(dump, NULL, 10) : 0;
        if (g_vk.gpu_dump_target_frame == 0 && dump && dump[0]) {
            g_vk.gpu_dump_target_frame = 1;
        }
        g_vk.gpu_dump_checked = 1;
    }

    g_vk.gpu_present_count++;
    if (!g_vk.gpu_dump_target_frame ||
        g_vk.gpu_present_count != g_vk.gpu_dump_target_frame ||
        !g_vk.gpu_frame_valid || !g_vk.render_image ||
        !g_vk.render_width || !g_vk.render_height) {
        return;
    }

    VkDeviceSize size = (VkDeviceSize)g_vk.render_width * g_vk.render_height * 4;
    if (!ensure_staging_buffer(size)) return;

    VkCommandBuffer cmd = begin_one_time_commands();
    if (!cmd) return;
    image_barrier(cmd, g_vk.render_image, g_vk.render_layout,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy;
    memset(&copy, 0, sizeof(copy));
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = g_vk.render_width;
    copy.imageExtent.height = g_vk.render_height;
    copy.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, g_vk.render_image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           g_vk.staging_buffer, 1, &copy);
    image_barrier(cmd, g_vk.render_image,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_ACCESS_TRANSFER_READ_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    end_one_time_commands(cmd);
    g_vk.render_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    void *mapped = NULL;
    if (vkMapMemory(g_vk.device, g_vk.staging_memory, 0, size, 0, &mapped) != VK_SUCCESS) {
        return;
    }
    FILE *f = fopen("/tmp/xboxrecomp_vulkan_frame.ppm", "wb");
    if (f) {
        const uint8_t *row = (const uint8_t *)mapped;
        fprintf(f, "P6\n%u %u\n255\n", g_vk.render_width, g_vk.render_height);
        for (uint32_t y = 0; y < g_vk.render_height; y++) {
            for (uint32_t x = 0; x < g_vk.render_width; x++) {
                const uint8_t *p = row + ((size_t)y * g_vk.render_width + x) * 4;
                uint8_t rgb[3] = { p[2], p[1], p[0] };
                fwrite(rgb, 1, sizeof(rgb), f);
            }
        }
        fclose(f);
        fprintf(stderr, "[D3D8/Vulkan] Dumped native frame %u to /tmp/xboxrecomp_vulkan_frame.ppm\n",
                g_vk.gpu_present_count);
    }
    vkUnmapMemory(g_vk.device, g_vk.staging_memory);
}

static void fit_rect(uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h,
                     VkOffset3D *dst0, VkOffset3D *dst1)
{
    uint64_t scale_x_num = dst_w;
    uint64_t scale_x_den = src_w ? src_w : 1;
    uint64_t scale_y_num = dst_h;
    uint64_t scale_y_den = src_h ? src_h : 1;
    uint64_t scale_num = scale_x_num * scale_y_den <= scale_y_num * scale_x_den ?
                         scale_x_num : scale_y_num;
    uint64_t scale_den = scale_x_num * scale_y_den <= scale_y_num * scale_x_den ?
                         scale_x_den : scale_y_den;
    uint32_t fit_w = (uint32_t)(((uint64_t)src_w * scale_num) / scale_den);
    uint32_t fit_h = (uint32_t)(((uint64_t)src_h * scale_num) / scale_den);
    if (!fit_w) fit_w = 1;
    if (!fit_h) fit_h = 1;
    if (fit_w > dst_w) fit_w = dst_w;
    if (fit_h > dst_h) fit_h = dst_h;
    uint32_t off_x = (dst_w - fit_w) / 2;
    uint32_t off_y = (dst_h - fit_h) / 2;
    dst0->x = (int32_t)off_x;
    dst0->y = (int32_t)off_y;
    dst0->z = 0;
    dst1->x = (int32_t)(off_x + fit_w);
    dst1->y = (int32_t)(off_y + fit_h);
    dst1->z = 1;
}

static void record_present(uint32_t image_index, int copy_pixels,
                           uint32_t copy_width, uint32_t copy_height,
                           int use_gpu_frame)
{
    VkCommandBuffer cmd = g_vk.command_buffers[image_index];

    VkCommandBufferBeginInfo begin;
    memset(&begin, 0, sizeof(begin));
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    VkImageMemoryBarrier to_clear;
    memset(&to_clear, 0, sizeof(to_clear));
    to_clear.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_clear.srcAccessMask = 0;
    to_clear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_clear.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_clear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_clear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_clear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_clear.image = g_vk.images[image_index];
    to_clear.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_clear.subresourceRange.levelCount = 1;
    to_clear.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, NULL, 0, NULL, 1, &to_clear);

    if (use_gpu_frame && g_vk.render_image) {
        VkClearColorValue color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
        VkImageSubresourceRange range;
        memset(&range, 0, sizeof(range));
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = 1;
        vkCmdClearColorImage(cmd, g_vk.images[image_index],
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &color, 1, &range);

        image_barrier(cmd, g_vk.render_image, g_vk.render_layout,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_ACCESS_TRANSFER_READ_BIT,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkImageBlit blit;
        memset(&blit, 0, sizeof(blit));
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[1].x = (int32_t)g_vk.render_width;
        blit.srcOffsets[1].y = (int32_t)g_vk.render_height;
        blit.srcOffsets[1].z = 1;
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.layerCount = 1;
        fit_rect(g_vk.render_width, g_vk.render_height,
                 g_vk.extent.width, g_vk.extent.height,
                 &blit.dstOffsets[0], &blit.dstOffsets[1]);
        vkCmdBlitImage(cmd,
                       g_vk.render_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       g_vk.images[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit, VK_FILTER_NEAREST);

        image_barrier(cmd, g_vk.render_image,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_ACCESS_TRANSFER_READ_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        g_vk.render_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    } else if (copy_pixels && g_vk.staging_buffer) {
        VkBufferImageCopy copy;
        memset(&copy, 0, sizeof(copy));
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent.width = copy_width;
        copy.imageExtent.height = copy_height;
        copy.imageExtent.depth = 1;
        vkCmdCopyBufferToImage(cmd, g_vk.staging_buffer, g_vk.images[image_index],
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    } else {
        VkClearColorValue color = { { 0.02f, 0.03f, 0.04f, 1.0f } };
        VkImageSubresourceRange range;
        memset(&range, 0, sizeof(range));
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = 1;
        vkCmdClearColorImage(cmd, g_vk.images[image_index],
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &color, 1, &range);
    }

    VkImageMemoryBarrier to_present = to_clear;
    to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_present.dstAccessMask = 0;
    to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                         0, NULL, 0, NULL, 1, &to_present);

    vkEndCommandBuffer(cmd);
}

int d3d8_vulkan_host_init(uint32_t width, uint32_t height)
{
    if (g_vk.initialized) return 1;
    memset(&g_vk, 0, sizeof(g_vk));

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "[D3D8/Vulkan] SDL video init failed: %s\n", SDL_GetError());
        return 0;
    }

    width = width ? width : 640;
    height = height ? height : 480;
    g_vk.window = SDL_CreateWindow("xboxrecomp",
                                   SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   (int)width, (int)height,
                                   SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!g_vk.window) {
        fprintf(stderr, "[D3D8/Vulkan] SDL window creation failed: %s\n", SDL_GetError());
        return 0;
    }

    unsigned ext_count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(g_vk.window, &ext_count, NULL)) {
        fprintf(stderr, "[D3D8/Vulkan] Could not query SDL Vulkan extensions: %s\n", SDL_GetError());
        return 0;
    }
    const char **extensions = (const char **)calloc(ext_count, sizeof(*extensions));
    if (!extensions) return 0;
    SDL_Vulkan_GetInstanceExtensions(g_vk.window, &ext_count, extensions);

    VkApplicationInfo app;
    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "xboxrecomp";
    app.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instance_info;
    memset(&instance_info, 0, sizeof(instance_info));
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app;
    instance_info.enabledExtensionCount = ext_count;
    instance_info.ppEnabledExtensionNames = extensions;

    VkResult result = vkCreateInstance(&instance_info, NULL, &g_vk.instance);
    free(extensions);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[D3D8/Vulkan] vkCreateInstance failed (%d)\n", result);
        return 0;
    }

    if (!SDL_Vulkan_CreateSurface(g_vk.window, g_vk.instance, &g_vk.surface)) {
        fprintf(stderr, "[D3D8/Vulkan] SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        return 0;
    }

    if (!pick_device() || !create_device() || !create_swapchain(width, height)) {
        fprintf(stderr, "[D3D8/Vulkan] Vulkan device/swapchain creation failed\n");
        return 0;
    }

    g_vk.initialized = 1;
    fprintf(stderr, "[D3D8/Vulkan] Presentation host initialized (%ux%u)\n",
            g_vk.extent.width, g_vk.extent.height);
    return 1;
}

void d3d8_vulkan_host_present(void)
{
    d3d8_vulkan_host_present_bgra(NULL, 0, 0, 0);
}

void d3d8_vulkan_host_present_bgra(const void *pixels, uint32_t width,
                                   uint32_t height, uint32_t pitch)
{
    if (!g_vk.initialized) return;

    SDL_Event event;
    int needs_resize = 0;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            d3d8_vulkan_host_shutdown();
            return;
        }
        if (event.type == SDL_WINDOWEVENT &&
            (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
             event.window.event == SDL_WINDOWEVENT_RESIZED)) {
            needs_resize = 1;
        }
    }
    if (needs_resize) {
        recreate_swapchain();
    }

    vkWaitForFences(g_vk.device, 1, &g_vk.in_flight, VK_TRUE, UINT64_MAX);
    vkResetFences(g_vk.device, 1, &g_vk.in_flight);
    maybe_dump_gpu_frame();

    uint32_t image_index = 0;
    VkResult result = vkAcquireNextImageKHR(g_vk.device, g_vk.swapchain, UINT64_MAX,
                                            g_vk.image_available, VK_NULL_HANDLE,
                                            &image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return;

    uint32_t copy_width = 0;
    uint32_t copy_height = 0;
    uint32_t copy_pitch = 0;
    const void *copy_source = prepare_present_pixels(pixels, width, height, pitch,
                                                     &copy_width, &copy_height,
                                                     &copy_pitch);
    int copy_pixels = copy_source && copy_width && copy_height;
    if (copy_pixels && copy_width && copy_height) {
        VkDeviceSize upload_size = (VkDeviceSize)copy_width * copy_height * 4;
        if (ensure_staging_buffer(upload_size)) {
            void *mapped = NULL;
            if (vkMapMemory(g_vk.device, g_vk.staging_memory, 0, upload_size, 0, &mapped) == VK_SUCCESS) {
                const uint8_t *src = (const uint8_t *)copy_source;
                uint8_t *dst = (uint8_t *)mapped;
                uint32_t row_bytes = copy_width * 4;
                for (uint32_t y = 0; y < copy_height; y++) {
                    memcpy(dst + (size_t)y * row_bytes, src + (size_t)y * copy_pitch, row_bytes);
                }
                vkUnmapMemory(g_vk.device, g_vk.staging_memory);
            } else {
                copy_pixels = 0;
            }
        } else {
            copy_pixels = 0;
        }
    } else {
        copy_pixels = 0;
    }

    vkResetCommandBuffer(g_vk.command_buffers[image_index], 0);
    int use_gpu_frame = g_vk.gpu_frame_valid && g_vk.render_image;
    if (use_gpu_frame) {
        g_vk.present_gpu_count++;
    } else if (copy_pixels) {
        g_vk.present_copy_count++;
    } else {
        g_vk.present_clear_count++;
    }
    if (g_vk.gpu_present_count <= 8 || (g_vk.gpu_present_count % 120) == 0) {
        fprintf(stderr,
                "[D3D8/Vulkan] present #%u mode=%s gpu=%u copy=%u clear=%u rhw_draws=%u textured=%u vertices=%u copy_size=%ux%u\n",
                g_vk.gpu_present_count,
                use_gpu_frame ? "native" : (copy_pixels ? "copy" : "clear"),
                g_vk.present_gpu_count,
                g_vk.present_copy_count,
                g_vk.present_clear_count,
                g_vk.rhw_draw_count,
                g_vk.rhw_textured_draw_count,
                g_vk.rhw_vertex_count,
                copy_width,
                copy_height);
    }
    record_present(image_index, copy_pixels, copy_width, copy_height, use_gpu_frame);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit;
    memset(&submit, 0, sizeof(submit));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &g_vk.image_available;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &g_vk.command_buffers[image_index];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &g_vk.render_finished;

    if (vkQueueSubmit(g_vk.graphics_queue, 1, &submit, g_vk.in_flight) != VK_SUCCESS) {
        return;
    }

    VkPresentInfoKHR present;
    memset(&present, 0, sizeof(present));
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &g_vk.render_finished;
    present.swapchainCount = 1;
    present.pSwapchains = &g_vk.swapchain;
    present.pImageIndices = &image_index;
    result = vkQueuePresentKHR(g_vk.present_queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreate_swapchain();
    }
}

void d3d8_vulkan_host_set_window_title(const char *title)
{
    if (g_vk.window && title) {
        SDL_SetWindowTitle(g_vk.window, title);
    }
}

void d3d8_vulkan_host_shutdown(void)
{
    if (!g_vk.instance && !g_vk.window) return;

    if (g_vk.device) vkDeviceWaitIdle(g_vk.device);
    destroy_native_renderer();
    destroy_swapchain();
    if (g_vk.device && g_vk.staging_buffer) vkDestroyBuffer(g_vk.device, g_vk.staging_buffer, NULL);
    if (g_vk.device && g_vk.staging_memory) vkFreeMemory(g_vk.device, g_vk.staging_memory, NULL);
    free(g_vk.scaled_frame);
    if (g_vk.device && g_vk.in_flight) vkDestroyFence(g_vk.device, g_vk.in_flight, NULL);
    if (g_vk.device && g_vk.render_finished) vkDestroySemaphore(g_vk.device, g_vk.render_finished, NULL);
    if (g_vk.device && g_vk.image_available) vkDestroySemaphore(g_vk.device, g_vk.image_available, NULL);
    if (g_vk.device && g_vk.command_pool) vkDestroyCommandPool(g_vk.device, g_vk.command_pool, NULL);
    if (g_vk.device) vkDestroyDevice(g_vk.device, NULL);
    if (g_vk.instance && g_vk.surface) vkDestroySurfaceKHR(g_vk.instance, g_vk.surface, NULL);
    if (g_vk.instance) vkDestroyInstance(g_vk.instance, NULL);
    if (g_vk.window) SDL_DestroyWindow(g_vk.window);
    memset(&g_vk, 0, sizeof(g_vk));
}
