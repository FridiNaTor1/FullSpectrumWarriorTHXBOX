#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <xbox/xboxrecomp.h>
#include "recomp_funcs.h"

#define FSW_TH_ENTRY_POINT 0x0005EE09u
#define FSW_TH_XBE_PATH "game_files/default.xbe"
#define FSW_TH_GAME_DIR "game_files"

extern uint32_t g_esp;
extern ptrdiff_t g_xbox_mem_offset;

static bool load_file(const char *path, void **out_data, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    long size;
    void *data;

    if (!f) {
        fprintf(stderr, "failed to open %s\n", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return false;
    }

    data = malloc((size_t)size);
    if (!data) {
        fclose(f);
        return false;
    }

    if (fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return false;
    }

    fclose(f);
    *out_data = data;
    *out_size = (size_t)size;
    return true;
}

int main(int argc, char **argv)
{
    const char *xbe_path = argc > 1 ? argv[1] : FSW_TH_XBE_PATH;
    const char *game_dir = argc > 2 ? argv[2] : FSW_TH_GAME_DIR;
    void *xbe_data = NULL;
    size_t xbe_size = 0;
    recomp_func_t entry;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("Loading XBE: %s\n", xbe_path);
    if (!load_file(xbe_path, &xbe_data, &xbe_size)) {
        return 1;
    }

    if (!xbox_MemoryLayoutInit(xbe_data, xbe_size)) {
        free(xbe_data);
        return 1;
    }

    g_xbox_mem_offset = xbox_GetMemoryOffset();
    xbox_kernel_init();
    xbox_path_init(game_dir, NULL);
    xbox_kernel_bridge_init();
    g_esp = XBOX_STACK_TOP;

    entry = recomp_lookup_manual(FSW_TH_ENTRY_POINT);
    if (!entry) {
        entry = recomp_lookup(FSW_TH_ENTRY_POINT);
    }
    if (!entry) {
        fprintf(stderr, "entry point 0x%08X is not in the recomp dispatch table\n",
                FSW_TH_ENTRY_POINT);
        xbox_kernel_shutdown();
        xbox_MemoryLayoutShutdown();
        free(xbe_data);
        return 1;
    }

    printf("Starting entry point 0x%08X\n", FSW_TH_ENTRY_POINT);
    entry();
    printf("Game returned\n");

    xbox_kernel_shutdown();
    xbox_MemoryLayoutShutdown();
    free(xbe_data);
    return 0;
}
