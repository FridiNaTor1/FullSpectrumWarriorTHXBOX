#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "recomp_funcs.h"
#ifdef XBOXRECOMP_VULKAN_GRAPHICS
#include "d3d8_vulkan_host.h"
#endif

extern volatile uint64_t g_icall_count;
extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);

#define FSW_SPLASH_LOCK_VA   0x003FF100u
#define FSW_SPLASH_UNLOCK_VA 0x003FF110u
#define FSW_SPLASH_WIDTH     640u
#define FSW_SPLASH_HEIGHT    480u
#define FSW_SPLASH_PITCH     (FSW_SPLASH_WIDTH * 4u)

static uint32_t g_fsw_video_va;
static uint32_t g_fsw_splash_surface_va;
static uint32_t g_fsw_splash_pixels_va;

static int fsw_va_is_valid(uint32_t va)
{
    return va >= 0x00010000u && va < 0x04000000u;
}

static void fsw_zero_xbox_block(uint32_t va, uint32_t size)
{
    if (!fsw_va_is_valid(va)) return;
    for (uint32_t i = 0; i < size; i += 4) {
        MEM32(va + i) = 0;
    }
}

uint32_t fsw_ensure_splash_surface(void)
{
    if (!fsw_va_is_valid(g_fsw_video_va)) {
        g_fsw_video_va = xbox_HeapAlloc(0x7000, 16);
        fsw_zero_xbox_block(g_fsw_video_va, 0x7000);
        MEM32(0x5FA8E8) = g_fsw_video_va;
    } else {
        MEM32(0x5FA8E8) = g_fsw_video_va;
    }

    if (!fsw_va_is_valid(g_fsw_splash_surface_va)) {
        uint32_t vtable = xbox_HeapAlloc(0x40, 16);
        g_fsw_splash_surface_va = xbox_HeapAlloc(0x40, 16);
        g_fsw_splash_pixels_va = xbox_HeapAlloc(FSW_SPLASH_PITCH * FSW_SPLASH_HEIGHT, 16);

        fsw_zero_xbox_block(vtable, 0x40);
        fsw_zero_xbox_block(g_fsw_splash_surface_va, 0x40);
        fsw_zero_xbox_block(g_fsw_splash_pixels_va, FSW_SPLASH_PITCH * FSW_SPLASH_HEIGHT);

        MEM32(vtable + 0x18) = FSW_SPLASH_LOCK_VA;
        MEM32(vtable + 0x1C) = FSW_SPLASH_UNLOCK_VA;
        MEM32(g_fsw_splash_surface_va + 0x00) = vtable;
        MEM32(g_fsw_splash_surface_va + 0x04) = g_fsw_splash_pixels_va;
        MEM32(g_fsw_splash_surface_va + 0x08) = FSW_SPLASH_WIDTH;
        MEM32(g_fsw_splash_surface_va + 0x0C) = FSW_SPLASH_HEIGHT;
        MEM32(g_fsw_splash_surface_va + 0x10) = FSW_SPLASH_PITCH;
    }

    if (fsw_va_is_valid(g_fsw_video_va)) {
        MEM32(g_fsw_video_va + 0x6C0C) = g_fsw_splash_surface_va;
    }
    return g_fsw_video_va;
}

static void fsw_splash_surface_lock(void)
{
    (void)MEM32(g_esp + 4); /* flags */
    if (fsw_va_is_valid(g_ecx) && fsw_va_is_valid(MEM32(g_ecx + 0x04))) {
        g_eax = MEM32(g_ecx + 0x04);
    } else {
        g_eax = g_fsw_splash_pixels_va;
    }
    g_esp += 8; return; /* ret 4 */
}

static void fsw_splash_surface_unlock(void)
{
#ifdef XBOXRECOMP_VULKAN_GRAPHICS
    uint32_t pixels = fsw_va_is_valid(g_ecx) ? MEM32(g_ecx + 0x04) : g_fsw_splash_pixels_va;
    if (fsw_va_is_valid(pixels)) {
        d3d8_vulkan_host_present_bgra((const void *)XBOX_PTR(pixels),
                                      FSW_SPLASH_WIDTH,
                                      FSW_SPLASH_HEIGHT,
                                      FSW_SPLASH_PITCH);
    }
#endif
    g_eax = 0;
    g_esp += 8; return; /* ret 4 */
}

recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    switch (xbox_va) {
    case FSW_SPLASH_LOCK_VA:
        return fsw_splash_surface_lock;
    case FSW_SPLASH_UNLOCK_VA:
        return fsw_splash_surface_unlock;
    default:
        break;
    }
    return NULL;
}

void recomp_icall_fail_log(uint32_t xbox_va)
{
    fprintf(stderr, "[icall] unresolved target 0x%08X after %llu indirect calls\n",
            xbox_va, (unsigned long long)g_icall_count);
}

void recomp_missing_target(uint32_t xbox_va)
{
    fprintf(stderr, "[recomp] missing direct target 0x%08X\n", xbox_va);
}
