/**
 * Xbox Memory Layout Implementation
 *
 * Maps the XBE data sections to their expected virtual addresses on Windows.
 * This is critical for the recompiled code which references globals by
 * absolute address (e.g., mov eax, [0x004D532C]).
 *
 * Implementation:
 * 1. VirtualAlloc a contiguous region at XBOX_BASE_ADDRESS
 * 2. Copy .rdata and initialized .data from the XBE
 * 3. Zero-fill the BSS region
 * 4. Set memory protection (read-only for .rdata)
 */

#include "xbox_memory_layout.h"
#include "kernel.h"
#include <stdio.h>
#include <string.h>

/* XBE header field offsets (per xboxdevwiki.net/Xbe) */
#define XBE_MAGIC_OFFSET        0x0000
#define XBE_BASE_ADDR_OFFSET    0x0104
#define XBE_HEADER_SIZE_OFFSET  0x0108
#define XBE_SECTION_COUNT_OFFSET 0x011C
#define XBE_SECTION_HEADERS_OFFSET 0x0120
#define XBE_THUNK_RETAIL_XOR  0x5B6D40B6u
#define XBE_THUNK_DEBUG_XOR   0xEFB1F152u

/* XBE section header layout (56 bytes each) */
#define SECTHDR_FLAGS       0x00
#define SECTHDR_VA          0x04
#define SECTHDR_VSIZE       0x08
#define SECTHDR_RAW_OFFSET  0x0C
#define SECTHDR_RAW_SIZE    0x10
#define SECTHDR_NAME_ADDR   0x14
#define SECTHDR_SIZE        56

static void *g_memory_base = NULL;
static size_t g_memory_size = 0;
static ptrdiff_t g_memory_offset = 0;  /* actual_base - XBOX_BASE_ADDRESS */

/* File mapping handle for the Xbox memory region.
 * Using CreateFileMapping + MapViewOfFileEx allows mirror views to alias
 * the same physical pages as the base region, so writes to mirror addresses
 * (which wrap modulo 64 MB on real Xbox hardware) correctly modify the
 * underlying data. */
static HANDLE g_mapping_handle = NULL;

/* Mirror view pointers for cleanup */
static void *g_mirror_views[XBOX_NUM_MIRRORS] = {0};

/* Separate allocation for Xbox kernel address space (0x80010000+).
 * Some RenderWare code reads the kernel PE header to detect features. */
static void *g_kernel_memory = NULL;

/* Global offset accessible by recompiled code (via recomp_types.h) */
ptrdiff_t g_xbox_mem_offset = 0;

/* Global registers for recompiled code (via recomp_types.h) */
uint32_t g_eax = 0, g_ecx = 0, g_edx = 0, g_esp = 0;
uint32_t g_ebx = 0, g_esi = 0, g_edi = 0;

/* SEH frame pointer bridge (see recomp_types.h for explanation) */
uint32_t g_seh_ebp = 0;

/* ICALL trace ring buffer */
volatile uint32_t g_icall_trace[16] = {0};
volatile uint32_t g_icall_trace_idx = 0;
volatile uint64_t g_icall_count = 0;

BOOL xbox_MemoryLayoutInit(const void *xbe_data, size_t xbe_size)
{
    DWORD old_protect;
    const uint8_t *xbe = (const uint8_t *)xbe_data;

    if (g_memory_base) {
        fprintf(stderr, "xbox_MemoryLayoutInit: already initialized\n");
        return FALSE;
    }

    /*
     * Calculate the full range we need to map.
     * From XBOX_MAP_START (0x0) to the end of the furthest section.
     * This includes low memory (KPCR at 0x0-0xFF) which game code reads
     * from, the XBE sections, and the simulated stack.
     */
    /* Map the full 64MB Xbox address space (covers all sections + stack + heap) */
    g_memory_size = XBOX_TOTAL_RAM;

    /*
     * Create a file mapping backed by the page file.
     *
     * Using file mapping instead of VirtualAlloc allows us to map the same
     * physical pages at multiple virtual addresses via MapViewOfFileEx.
     * This is critical for the Xbox RAM mirror: the Xbox memory controller
     * uses a 26-bit address bus, so ALL addresses wrap modulo 64 MB.
     * Code that writes to address 0x20000448 is really writing to 0x00000448.
     * With file mapping views, we create aliased mappings at 64 MB intervals
     * that all point to the same physical memory.
     */
    g_mapping_handle = CreateFileMappingA(
        INVALID_HANDLE_VALUE,   /* page file backed */
        NULL,                   /* default security */
        PAGE_READWRITE,         /* read-write access */
        0,                      /* high DWORD of size */
        (DWORD)g_memory_size,   /* low DWORD of size (64 MB) */
        NULL                    /* unnamed mapping */
    );
    if (!g_mapping_handle) {
        fprintf(stderr, "xbox_MemoryLayoutInit: CreateFileMapping failed (error %lu)\n",
                GetLastError());
        return FALSE;
    }

    /*
     * Map the base view at the desired virtual address.
     * Try the original Xbox base address first. If that fails (common on
     * Windows 11 where low addresses are often reserved), try page-aligned
     * addresses upward until we find a free region.
     */
    {
        static const uintptr_t try_bases[] = {
            XBOX_BASE_ADDRESS,      /* 0x00010000 - original Xbox address */
            0x00800000,             /* 8 MB - above typical PEB/TEB region */
            0x01000000,             /* 16 MB */
            0x02000000,             /* 32 MB */
            0x10000000,             /* 256 MB */
            0,                      /* sentinel - let OS choose */
        };

        for (int i = 0; try_bases[i] != 0 || i == 0; i++) {
            LPVOID hint = try_bases[i] ? (LPVOID)try_bases[i] : NULL;
            g_memory_base = MapViewOfFileEx(
                g_mapping_handle,
                FILE_MAP_ALL_ACCESS,
                0, 0,           /* offset into mapping */
                g_memory_size,  /* size */
                hint            /* desired base address */
            );
            if (g_memory_base) {
                if (try_bases[i] != 0 && (uintptr_t)g_memory_base != try_bases[i]) {
                    /* OS gave us a different address, retry */
                    UnmapViewOfFile(g_memory_base);
                    g_memory_base = NULL;
                    continue;
                }
                break;
            }
        }
    }

    if (!g_memory_base) {
        fprintf(stderr, "xbox_MemoryLayoutInit: failed to map base view (%zu KB)\n",
                g_memory_size / 1024);
        CloseHandle(g_mapping_handle);
        g_mapping_handle = NULL;
        return FALSE;
    }

    g_memory_offset = (uintptr_t)g_memory_base - XBOX_MAP_START;

    if (g_memory_offset == 0) {
        fprintf(stderr, "xbox_MemoryLayoutInit: mapped %zu KB at 0x%08X (original Xbox address)\n",
                g_memory_size / 1024, XBOX_MAP_START);
    } else {
        fprintf(stderr, "xbox_MemoryLayoutInit: mapped %zu KB at 0x%p (offset %+td from Xbox base)\n",
                g_memory_size / 1024, g_memory_base, g_memory_offset);
    }

    /*
     * Helper macro: convert Xbox VA to actual mapped address.
     * When g_memory_offset == 0 (ideal case), this is identity.
     */
    #define XBOX_VA(va) ((void *)((uintptr_t)(va) + g_memory_offset))

    /*
     * Copy XBE header to base address.
     * The Xbox kernel maps the XBE image header at 0x00010000.
     * Game code reads kernel thunk table, certificate data, and
     * section info from this region.
     */
    {
        /* XBE header size is at file offset 0x0108 (SizeOfImageHeader) */
        DWORD header_size = 0;
        if (xbe_size >= 0x10C) {
            header_size = *(const DWORD *)(xbe + 0x0108);
        }
        if (header_size == 0 || header_size > 0x10000)
            header_size = 0x1000;  /* fallback: 4KB */
        if (header_size > xbe_size)
            header_size = (DWORD)xbe_size;
        memcpy(XBOX_VA(XBOX_BASE_ADDRESS), xbe, header_size);
        fprintf(stderr, "  XBE header: %u bytes at %p (Xbox VA 0x%08X)\n",
                header_size, XBOX_VA(XBOX_BASE_ADDRESS), XBOX_BASE_ADDRESS);
    }

    /*
     * Dynamically load ALL XBE sections by parsing the section headers.
     *
     * This replaces the old approach of hardcoding section addresses for
     * a specific game (Burnout 3). By reading the section table from the
     * XBE header, any game's sections are loaded automatically.
     *
     * Every section is copied to its original Xbox VA:
     * - .text: needed because memory walkers may scan code pages
     * - .rdata: constants, vtables, kernel thunk table
     * - .data: global variables (initialized portion from XBE, BSS zeroed)
     * - XDK library sections (D3D, DSOUND, WMADEC, XPP, etc.)
     * - DOLBY, BINK, XTIMAGE, etc.
     */
    {
        DWORD base_addr = *(const DWORD *)(xbe + XBE_BASE_ADDR_OFFSET);
        DWORD num_sections = *(const DWORD *)(xbe + XBE_SECTION_COUNT_OFFSET);
        DWORD sect_headers_va = *(const DWORD *)(xbe + XBE_SECTION_HEADERS_OFFSET);
        DWORD sect_headers_off = sect_headers_va - base_addr;
        int sections_loaded = 0;
        size_t total_bytes = 0;

        if (num_sections > 64) num_sections = 64;  /* sanity cap */

        fprintf(stderr, "  XBE sections: %u (headers at file offset 0x%08X)\n",
                num_sections, sect_headers_off);

        for (DWORD si = 0; si < num_sections; si++) {
            if (sect_headers_off + (si + 1) * SECTHDR_SIZE > xbe_size) break;

            const uint8_t *sh = xbe + sect_headers_off + si * SECTHDR_SIZE;
            DWORD sec_va       = *(const DWORD *)(sh + SECTHDR_VA);
            DWORD sec_vsize    = *(const DWORD *)(sh + SECTHDR_VSIZE);
            DWORD sec_raw_off  = *(const DWORD *)(sh + SECTHDR_RAW_OFFSET);
            DWORD sec_raw_size = *(const DWORD *)(sh + SECTHDR_RAW_SIZE);
            DWORD sec_name_va  = *(const DWORD *)(sh + SECTHDR_NAME_ADDR);

            /* Read section name from XBE header */
            const char *sec_name = "?";
            DWORD name_off = sec_name_va - base_addr;
            if (name_off < xbe_size && name_off + 8 <= xbe_size)
                sec_name = (const char *)(xbe + name_off);

            /* Validate: section must fit within our 64MB mapped region */
            if (sec_va < XBOX_BASE_ADDRESS || sec_va + sec_vsize > XBOX_TOTAL_RAM)
                continue;

            /* Determine copy size (raw_size may exceed vsize due to alignment) */
            DWORD copy_size = (sec_raw_size < sec_vsize) ? sec_raw_size : sec_vsize;

            /* Zero the full virtual size first (handles BSS) */
            memset(XBOX_VA(sec_va), 0, sec_vsize);

            /* Copy initialized data from XBE */
            if (copy_size > 0 && sec_raw_off + copy_size <= xbe_size) {
                memcpy(XBOX_VA(sec_va), xbe + sec_raw_off, copy_size);
            }

            sections_loaded++;
            total_bytes += copy_size;

            fprintf(stderr, "  [%2u] %-12s VA=0x%08X vsize=%-8u raw=0x%08X rsize=%-8u%s\n",
                    si, sec_name, sec_va, sec_vsize, sec_raw_off, sec_raw_size,
                    (sec_raw_size < sec_vsize) ? " (BSS)" : "");
        }

        fprintf(stderr, "  Loaded %d/%u sections (%zu bytes total)\n",
                sections_loaded, num_sections, total_bytes);
    }

    /*
     * Parse the kernel thunk table address from the XBE header.
     * KernelImageThunkAddress is XOR-encrypted with different keys for
     * retail and debug builds. Try both and use the first plausible table.
     */
    if (xbe_size >= 0x015C) {
        uint32_t thunk_raw = *(const uint32_t *)(xbe + 0x0158);
        const uint32_t candidates[] = {
            thunk_raw ^ XBE_THUNK_RETAIL_XOR,
            thunk_raw ^ XBE_THUNK_DEBUG_XOR,
        };
        uint32_t thunk_va = 0;
        uint32_t thunk_count = 0;

        for (size_t c = 0; c < sizeof(candidates) / sizeof(candidates[0]); c++) {
            uint32_t candidate = candidates[c];
            uint32_t count = 0;
            if (candidate < XBOX_BASE_ADDRESS || candidate >= XBOX_TOTAL_RAM) {
                continue;
            }
            for (uint32_t t = 0; t < 366; t++) {
                uint32_t entry = *(volatile uint32_t *)((uintptr_t)(candidate + t * 4) + g_memory_offset);
                if (entry == 0) break;
                count++;
            }
            if (count > 0) {
                thunk_va = candidate;
                thunk_count = count;
                break;
            }
        }

        if (thunk_va) {
            xbox_kernel_set_thunk_address(thunk_va, thunk_count);
            fprintf(stderr, "  Kernel thunks: %u entries at Xbox VA 0x%08X\n",
                    thunk_count, thunk_va);
        } else {
            fprintf(stderr, "  WARNING: kernel thunk VA out of range (raw=0x%08X)\n",
                    thunk_raw);
        }
    }

    /*
     * NOTE: .rdata is NOT set read-only.
     * VirtualProtect rounds to page boundaries, and the .rdata end (0x003B2454)
     * and .data start (0x003B2360) share the same 4KB page (0x003B2000-0x003B2FFF).
     * Making .rdata read-only also makes the first ~0xCA0 bytes of .data read-only,
     * which causes game initialization code to fault when writing to .data globals
     * in that overlap range.
     */
    (void)old_protect;

    #undef XBOX_VA

    /* Set the global offset for recompiled code MEM macros */
    g_xbox_mem_offset = g_memory_offset;

    /*
     * Initialize the Xbox stack for recompiled code.
     * The stack area lives at XBOX_STACK_BASE in Xbox address space.
     * g_esp is the global stack pointer shared by all translated functions.
     */
    g_esp = XBOX_STACK_TOP;
    fprintf(stderr, "  Stack: %u KB at Xbox VA 0x%08X (ESP = 0x%08X)\n",
            XBOX_STACK_SIZE / 1024, XBOX_STACK_BASE, g_esp);

    /*
     * Populate the fake Thread Information Block (TIB) at Xbox VA 0x0.
     *
     * The original Xbox code uses fs:[offset] to read per-thread data,
     * but the recompiler drops the fs: segment prefix and generates
     * MEM32(offset) instead. Since we mapped low memory (0x0-0xFFFF),
     * we populate the TIB fields that game code accesses:
     *
     *   fs:[0x00] = SEH exception list (-1 = end of chain)
     *   fs:[0x04] = stack base (top of stack)
     *   fs:[0x08] = stack limit (bottom of stack)
     *   fs:[0x18] = self pointer (TIB address)
     *   fs:[0x20] = KPCR Prcb pointer (→ fake structure)
     *   fs:[0x28] = TLS / RW engine context pointer
     *
     * We use free space in the BSS area for the fake structures.
     */
    {
        #define XBOX_VA(va) ((void *)((uintptr_t)(va) + g_memory_offset))
        #define MEM32_INIT(va, val) (*(uint32_t *)XBOX_VA(va) = (uint32_t)(val))

        /* Fake TIB at address 0x0 */
        MEM32_INIT(0x00, 0xFFFFFFFF);       /* SEH: end of chain */
        MEM32_INIT(0x04, XBOX_STACK_TOP);   /* Stack base (high address) */
        MEM32_INIT(0x08, XBOX_STACK_BASE);  /* Stack limit (low address) */
        MEM32_INIT(0x18, 0x00000000);       /* Self pointer (TIB at VA 0) */

        /*
         * fs:[0x20] - On Xbox KPCR, this is the Prcb pointer.
         * Game code reads [fs:[0x20] + 0x250] which on the real Xbox
         * accesses a D3D cache structure. We set it to 0 so the read
         * at offset 0x250 returns 0, causing the cache init to be skipped.
         */
        MEM32_INIT(0x20, 0x00000000);

        /*
         * fs:[0x28] - Thread local storage / RW engine context.
         * The RW engine reads [fs:[0x28] + 0x28] to get a pointer
         * to its data area. We allocate a fake structure at 0x00760000
         * (in the BSS area) and a data buffer at 0x00700000.
         */
        #define FAKE_TLS_VA     0x00760000  /* Fake TLS structure (in BSS) */
        #define FAKE_RWDATA_VA  0x00700000  /* RW engine data area (in BSS) */

        MEM32_INIT(0x28, FAKE_TLS_VA);
        /* TLS[0x28] = pointer to RW data area */
        MEM32_INIT(FAKE_TLS_VA + 0x28, FAKE_RWDATA_VA);

        fprintf(stderr, "  TIB: fake TIB at VA 0x0, TLS at 0x%08X, RW data at 0x%08X\n",
                FAKE_TLS_VA, FAKE_RWDATA_VA);

        #undef FAKE_TLS_VA
        #undef FAKE_RWDATA_VA
        #undef MEM32_INIT
        #undef XBOX_VA
    }

    /*
     * Allocate a page at Xbox kernel address space (0x80010000).
     *
     * RenderWare's Xbox driver code (xbcache.c) reads MEM32(0x8001003C)
     * to parse the Xbox kernel's PE header and find the INIT section for
     * CPU cache line sizing. On PC, we provide a minimal fake PE header
     * with 0 sections so the function gracefully skips the cache init.
     *
     * The actual native address is 0x80010000 + g_memory_offset.
     */
    {
        #define XBOX_KERNEL_BASE 0x80010000u
        #define KERNEL_PAGE_SIZE 4096
        uintptr_t kernel_native = XBOX_KERNEL_BASE + g_memory_offset;
        g_kernel_memory = VirtualAlloc(
            (LPVOID)kernel_native,
            KERNEL_PAGE_SIZE,
            MEM_RESERVE | MEM_COMMIT,
            PAGE_READWRITE
        );
        if (g_kernel_memory) {
            /* Zero-fill then set e_lfanew = 0x80 (offset to PE header).
             * With the rest zeroed, NumberOfSections = 0 and the INIT
             * section search finds nothing, which is the safe path. */
            memset(g_kernel_memory, 0, KERNEL_PAGE_SIZE);
            *(uint32_t *)((uint8_t *)g_kernel_memory + 0x3C) = 0x80;  /* e_lfanew */
            fprintf(stderr, "  Kernel: fake PE header at Xbox VA 0x%08X (native %p)\n",
                    XBOX_KERNEL_BASE, g_kernel_memory);
        } else {
            fprintf(stderr, "  WARNING: could not map Xbox kernel VA 0x%08X\n",
                    XBOX_KERNEL_BASE);
        }
        #undef XBOX_KERNEL_BASE
        #undef KERNEL_PAGE_SIZE
    }

    /* Initialize the dynamic heap. */
    fprintf(stderr, "  Heap: %u MB at Xbox VA 0x%08X-0x%08X\n",
            XBOX_HEAP_SIZE / (1024 * 1024), XBOX_HEAP_BASE,
            XBOX_HEAP_BASE + XBOX_HEAP_SIZE);

    /*
     * Map mirror views of the 64 MB region.
     *
     * On retail Xbox, physical RAM wraps at 64 MB due to the 26-bit
     * address bus. Address 0x04070000 reads the same data as 0x00070000.
     * The RenderWare engine's memory walker crosses 64 MB and accesses
     * mirrored data for an extended walk covering 256+ MB of virtual
     * addresses. Game init code also writes large data structures past
     * 64 MB that on real hardware wrap into physical RAM.
     *
     * We map additional views of the SAME file mapping section at 64 MB
     * intervals. All views alias the same physical pages, so reads and
     * writes at any mirror address correctly access the base data.
     */
    {
        int mirrors_ok = 0;
        for (int m = 0; m < XBOX_NUM_MIRRORS; m++) {
            uintptr_t mirror_base = (uintptr_t)g_memory_base +
                                    (uintptr_t)(m + 1) * g_memory_size;
            g_mirror_views[m] = MapViewOfFileEx(
                g_mapping_handle,
                FILE_MAP_ALL_ACCESS,
                0, 0,
                g_memory_size,
                (LPVOID)mirror_base
            );
            if (g_mirror_views[m]) {
                mirrors_ok++;
            } else {
                fprintf(stderr, "  Mirror %d: FAILED at %p (error %lu)\n",
                        m + 1, (void *)mirror_base, GetLastError());
            }
        }
        fprintf(stderr, "  RAM mirror: %d/%d views mapped (covers %d MB)\n",
                mirrors_ok, XBOX_NUM_MIRRORS,
                (int)((mirrors_ok + 1) * g_memory_size / (1024 * 1024)));
    }

    fprintf(stderr, "xbox_MemoryLayoutInit: complete\n");
    return TRUE;
}

void xbox_MemoryLayoutShutdown(void)
{
    if (g_kernel_memory) {
        VirtualFree(g_kernel_memory, 0, MEM_RELEASE);
        g_kernel_memory = NULL;
    }
    /* Unmap mirror views first */
    for (int m = 0; m < XBOX_NUM_MIRRORS; m++) {
        if (g_mirror_views[m]) {
            UnmapViewOfFile(g_mirror_views[m]);
            g_mirror_views[m] = NULL;
        }
    }
    /* Unmap base view */
    if (g_memory_base) {
        UnmapViewOfFile(g_memory_base);
        g_memory_base = NULL;
        g_memory_size = 0;
    }
    /* Close file mapping handle */
    if (g_mapping_handle) {
        CloseHandle(g_mapping_handle);
        g_mapping_handle = NULL;
    }
    fprintf(stderr, "xbox_MemoryLayoutShutdown: released\n");
}

BOOL xbox_IsXboxAddress(uintptr_t address)
{
    return (address >= XBOX_BASE_ADDRESS &&
            address < XBOX_BASE_ADDRESS + g_memory_size);
}

void *xbox_GetMemoryBase(void)
{
    return g_memory_base;
}

ptrdiff_t xbox_GetMemoryOffset(void)
{
    return g_memory_offset;
}

/* ── Dynamic heap allocator ────────────────────────────────
 *
 * Simple bump allocator for MmAllocateContiguousMemory and similar.
 * Returns Xbox VAs within the mapped region so MEM32() works correctly.
 * Bump allocator with a small free-list layered on top. Several games
 * release their shell/UI level before reserving the mission level; keeping
 * those blocks reusable is required to stay inside the Xbox's 64 MB window.
 */
static uint32_t g_heap_next = XBOX_HEAP_BASE;

static int g_heap_alloc_count = 0;

#define XBOX_HEAP_RECORD_MAX 65536

typedef struct XboxHeapRecord {
    uint32_t addr;
    uint32_t size;
    int free;
} XboxHeapRecord;

static XboxHeapRecord g_heap_records[XBOX_HEAP_RECORD_MAX];
static uint32_t g_heap_record_count = 0;

static void xbox_heap_add_record(uint32_t addr, uint32_t size, int free)
{
    if (size == 0 || g_heap_record_count >= XBOX_HEAP_RECORD_MAX) {
        return;
    }
    g_heap_records[g_heap_record_count].addr = addr;
    g_heap_records[g_heap_record_count].size = size;
    g_heap_records[g_heap_record_count].free = free;
    g_heap_record_count++;
}

static void xbox_heap_remove_record(uint32_t index)
{
    if (index >= g_heap_record_count) {
        return;
    }
    g_heap_record_count--;
    if (index != g_heap_record_count) {
        g_heap_records[index] = g_heap_records[g_heap_record_count];
    }
}

static void xbox_heap_coalesce_free_records(void)
{
    int merged;

    do {
        merged = 0;
        for (uint32_t i = 0; i < g_heap_record_count && !merged; i++) {
            XboxHeapRecord *a = &g_heap_records[i];
            uint32_t a_end;
            if (!a->free || a->size == 0) {
                continue;
            }
            a_end = a->addr + a->size;
            for (uint32_t j = i + 1; j < g_heap_record_count; j++) {
                XboxHeapRecord *b = &g_heap_records[j];
                uint32_t b_end;
                if (!b->free || b->size == 0) {
                    continue;
                }
                b_end = b->addr + b->size;
                if (a_end == b->addr) {
                    a->size += b->size;
                    xbox_heap_remove_record(j);
                    merged = 1;
                    break;
                }
                if (b_end == a->addr) {
                    a->addr = b->addr;
                    a->size += b->size;
                    xbox_heap_remove_record(j);
                    merged = 1;
                    break;
                }
            }
        }
    } while (merged);
}

static void xbox_heap_get_free_summary(uint32_t alignment,
                                       uint32_t *free_total,
                                       uint32_t *free_largest,
                                       uint32_t *free_records)
{
    uint32_t total = 0;
    uint32_t largest = 0;
    uint32_t records = 0;

    for (uint32_t i = 0; i < g_heap_record_count; i++) {
        const XboxHeapRecord *record = &g_heap_records[i];
        uint32_t aligned;
        uint32_t available;
        uint32_t record_end;
        if (!record->free || record->size == 0) {
            continue;
        }
        records++;
        total += record->size;
        aligned = (record->addr + alignment - 1) & ~(alignment - 1);
        record_end = record->addr + record->size;
        available = (aligned >= record->addr && aligned < record_end) ? record_end - aligned : 0;
        if (available > largest) {
            largest = available;
        }
    }

    *free_total = total;
    *free_largest = largest;
    *free_records = records;
}

static int xbox_heap_ranges_overlap(uint32_t a_addr, uint32_t a_size,
                                    uint32_t b_addr, uint32_t b_size)
{
    uint32_t a_end = a_addr + a_size;
    uint32_t b_end = b_addr + b_size;
    return a_size != 0 && b_size != 0 && a_addr < b_end && b_addr < a_end;
}

uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment)
{
    uint32_t result;

    if (alignment < 4) alignment = 4;

    /* Zero-byte allocations still need a unique address in the bump heap.
     * Keep normal small allocations small; inflating them burns through the
     * 48 MB Xbox heap during menu/UI setup. */
    if (size == 0) size = alignment;

    /* Align the next pointer */
    for (uint32_t i = 0; i < g_heap_record_count; i++) {
        uint32_t aligned;
        uint32_t original_addr;
        uint32_t original_size;
        uint32_t original_end;
        uint32_t allocated_end;
        XboxHeapRecord *record = &g_heap_records[i];
        if (!record->free || record->size == 0) {
            continue;
        }
        aligned = (record->addr + alignment - 1) & ~(alignment - 1);
        if (aligned >= record->addr &&
            aligned <= record->addr + record->size &&
            size <= (record->addr + record->size) - aligned) {
            original_addr = record->addr;
            original_size = record->size;
            original_end = original_addr + original_size;
            allocated_end = aligned + size;
            record->addr = aligned;
            record->size = size;
            record->free = 0;
            if (aligned > original_addr) {
                xbox_heap_add_record(original_addr, aligned - original_addr, 1);
            }
            if (allocated_end < original_end) {
                xbox_heap_add_record(allocated_end, original_end - allocated_end, 1);
            }
            memset((void *)((uintptr_t)aligned + g_memory_offset), 0, size);
            g_heap_alloc_count++;
            if (g_heap_alloc_count <= 16 || (g_heap_alloc_count % 256) == 0) {
                fprintf(stderr, "  [HEAP] #%d: size=%u align=%u -> 0x%08X..0x%08X (reused, used %u/%u)\n",
                        g_heap_alloc_count, size, alignment, aligned, aligned + size,
                        g_heap_next - XBOX_HEAP_BASE, XBOX_HEAP_SIZE);
                fflush(stderr);
            }
            return aligned;
        }
    }

    if (size >= 0x100000u) {
        for (uint32_t i = 0; i < g_heap_record_count; i++) {
            XboxHeapRecord *record = &g_heap_records[i];
            uint32_t aligned;
            uint32_t end;
            uint32_t blocker = XBOX_HEAP_BASE + XBOX_HEAP_SIZE;
            if (!record->free || record->size == 0) {
                continue;
            }
            aligned = (record->addr + alignment - 1) & ~(alignment - 1);
            if (aligned < record->addr || size > (XBOX_HEAP_BASE + XBOX_HEAP_SIZE) - aligned) {
                continue;
            }
            end = aligned + size;
            for (uint32_t j = 0; j < g_heap_record_count; j++) {
                XboxHeapRecord *other = &g_heap_records[j];
                if (other->free || other->size == 0) {
                    continue;
                }
                if (other->addr >= aligned && other->addr < blocker) {
                    blocker = other->addr;
                }
            }
            if (end > blocker) {
                continue;
            }
            for (uint32_t j = 0; j < g_heap_record_count; j++) {
                XboxHeapRecord *other = &g_heap_records[j];
                if (!other->free || other->size == 0) {
                    continue;
                }
                if (xbox_heap_ranges_overlap(other->addr, other->size, aligned, size)) {
                    other->free = 0;
                    other->size = 0;
                }
            }
            if (g_heap_record_count < XBOX_HEAP_RECORD_MAX) {
                g_heap_records[g_heap_record_count].addr = aligned;
                g_heap_records[g_heap_record_count].size = size;
                g_heap_records[g_heap_record_count].free = 0;
                g_heap_record_count++;
            }
            memset((void *)((uintptr_t)aligned + g_memory_offset), 0, size);
            g_heap_alloc_count++;
            if (g_heap_alloc_count <= 16 || (g_heap_alloc_count % 256) == 0) {
                fprintf(stderr, "  [HEAP] #%d: size=%u align=%u -> 0x%08X..0x%08X (coalesced, used %u/%u)\n",
                        g_heap_alloc_count, size, alignment, aligned, aligned + size,
                        g_heap_next - XBOX_HEAP_BASE, XBOX_HEAP_SIZE);
                fflush(stderr);
            }
            return aligned;
        }
    }

    result = (g_heap_next + alignment - 1) & ~(alignment - 1);

    if (size > (XBOX_HEAP_BASE + XBOX_HEAP_SIZE) - result) {
        static uint32_t oom_logs = 0;
        uint32_t guest_ret = 0;
        uint32_t free_total = 0;
        uint32_t free_largest = 0;
        uint32_t free_records = 0;
        xbox_heap_get_free_summary(alignment, &free_total, &free_largest, &free_records);
        if (g_memory_base && g_esp <= XBOX_TOTAL_RAM - 4) {
            guest_ret = *(volatile uint32_t *)((uintptr_t)g_esp + g_memory_offset);
        }
        if (oom_logs < 32 || (oom_logs % 8192) == 0) {
            fprintf(stderr,
                    "xbox_HeapAlloc: out of memory (requested %u, used %u/%u, free_total=%u, free_largest=%u, free_records=%u, records=%u, esp=0x%08X ret=0x%08X count=%u)\n",
                    size, g_heap_next - XBOX_HEAP_BASE, XBOX_HEAP_SIZE,
                    free_total, free_largest, free_records, g_heap_record_count,
                    g_esp, guest_ret, oom_logs + 1);
        }
        oom_logs++;
        return 0;
    }

    g_heap_next = result + size;
    if (g_heap_record_count < XBOX_HEAP_RECORD_MAX) {
        g_heap_records[g_heap_record_count].addr = result;
        g_heap_records[g_heap_record_count].size = size;
        g_heap_records[g_heap_record_count].free = 0;
        g_heap_record_count++;
    }

    /* Zero-fill the allocated block (Xbox memory is always zeroed) */
    memset((void *)((uintptr_t)result + g_memory_offset), 0, size);

    g_heap_alloc_count++;
    if (g_heap_alloc_count <= 16 || (g_heap_alloc_count % 256) == 0) {
        fprintf(stderr, "  [HEAP] #%d: size=%u align=%u -> 0x%08X..0x%08X (used %u/%u)\n",
                g_heap_alloc_count, size, alignment, result, result + size,
                g_heap_next - XBOX_HEAP_BASE, XBOX_HEAP_SIZE);
        fflush(stderr);
    }

    return result;
}

void xbox_HeapFree(uint32_t xbox_va)
{
    for (uint32_t i = 0; i < g_heap_record_count; i++) {
        if (g_heap_records[i].size != 0 && g_heap_records[i].addr == xbox_va) {
            g_heap_records[i].free = 1;
            xbox_heap_coalesce_free_records();
            return;
        }
    }
}

HANDLE xbox_GetMappingHandle(void)
{
    return g_mapping_handle;
}
