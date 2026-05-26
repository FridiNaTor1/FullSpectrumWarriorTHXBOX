/*
 * Portable D3D8 backend for native Linux bring-up.
 *
 * This preserves the Xbox D3D8 ABI so recompiled code can link and run
 * while using either the headless software path or the native Vulkan bridge.
 */
#include "d3d8_xbox.h"
#ifdef XBOXRECOMP_VULKAN_GRAPHICS
#include "d3d8_vulkan_host.h"
#endif
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct NullResource {
    union {
        IDirect3DVertexBuffer8 vb;
        IDirect3DIndexBuffer8 ib;
        IDirect3DTexture8 tex;
        IDirect3DSurface8 surf;
    } iface;
    LONG ref_count;
    BYTE *data;
    UINT size;
    UINT width;
    UINT height;
    UINT pitch;
    D3DFORMAT format;
    BYTE *rgba;
} NullResource;

static IDirect3D8 g_d3d8;
static IDirect3DDevice8 g_device;
static LONG g_d3d8_ref = 1;
static LONG g_device_ref = 1;
static D3DVIEWPORT8 g_viewport = { 0, 0, 640, 480, 0.0f, 1.0f };
static BYTE *g_framebuffer;
static float *g_depthbuffer;
static UINT g_fb_width;
static UINT g_fb_height;
static NullResource *g_bound_texture[4];
static NullResource *g_stream_vb;
static NullResource *g_index_ib;
static UINT g_stream_stride;
static UINT g_index_base;
static DWORD g_current_fvf;
static D3DMATRIX g_world_matrix;
static D3DMATRIX g_view_matrix;
static D3DMATRIX g_proj_matrix;
static BOOL g_z_enable = TRUE;
static BOOL g_z_write_enable = TRUE;
static DWORD g_cull_mode = D3DCULL_NONE;
static int g_dump_checked;
static uint32_t g_dump_target_frame;
static uint32_t g_dump_present_count;
#ifdef XBOXRECOMP_VULKAN_GRAPHICS
static int g_vulkan_native_checked;
static int g_vulkan_native_enabled = 1;
#endif

volatile int g_suppress_present = 0;

static const IDirect3D8Vtbl g_d3d8_vtbl;
static const IDirect3DDevice8Vtbl g_device_vtbl;
static const IDirect3DVertexBuffer8Vtbl g_vb_vtbl;
static const IDirect3DIndexBuffer8Vtbl g_ib_vtbl;
static const IDirect3DTexture8Vtbl g_tex_vtbl;
static const IDirect3DSurface8Vtbl g_surf_vtbl;

static UINT format_bpp(D3DFORMAT fmt)
{
    switch (fmt) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_LIN_A8R8G8B8:
    case D3DFMT_LIN_X8R8G8B8:
        return 4;
    case D3DFMT_R5G6B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_LIN_R5G6B5:
    case D3DFMT_LIN_A1R5G5B5:
    case D3DFMT_LIN_A4R4G4B4:
        return 2;
    case D3DFMT_A8:
    case D3DFMT_L8:
    case D3DFMT_P8:
        return 1;
    default:
        return 4;
    }
}

static UINT format_storage_size(D3DFORMAT fmt, UINT w, UINT h)
{
    UINT bw = (w + 3) / 4;
    UINT bh = (h + 3) / 4;
    switch (fmt) {
    case D3DFMT_DXT1:
        return bw * bh * 8;
    case D3DFMT_DXT3:
    case D3DFMT_DXT5:
        return bw * bh * 16;
    default:
        return w * h * format_bpp(fmt);
    }
}

static UINT format_pitch(D3DFORMAT fmt, UINT w)
{
    switch (fmt) {
    case D3DFMT_DXT1:
        return ((w + 3) / 4) * 8;
    case D3DFMT_DXT3:
    case D3DFMT_DXT5:
        return ((w + 3) / 4) * 16;
    default:
        return w * format_bpp(fmt);
    }
}

static uint32_t bgra_color(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
           ((uint32_t)g << 8) | (uint32_t)b;
}

static uint8_t expand5(uint32_t v) { return (uint8_t)((v << 3) | (v >> 2)); }
static uint8_t expand6(uint32_t v) { return (uint8_t)((v << 2) | (v >> 4)); }
static uint8_t expand4(uint32_t v) { return (uint8_t)((v << 4) | v); }

static void decode_565(uint16_t c, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = expand5((c >> 11) & 31);
    *g = expand6((c >> 5) & 63);
    *b = expand5(c & 31);
}

static void decode_texture(NullResource *r)
{
    if (!r || !r->width || !r->height) return;
    size_t out_size = (size_t)r->width * r->height * 4;
    if (!r->rgba) r->rgba = (BYTE *)calloc(1, out_size);
    if (!r->rgba) return;

    if (r->format == D3DFMT_A8R8G8B8 || r->format == D3DFMT_LIN_A8R8G8B8) {
        memcpy(r->rgba, r->data, out_size < r->size ? out_size : r->size);
        return;
    }
    if (r->format == D3DFMT_X8R8G8B8 || r->format == D3DFMT_LIN_X8R8G8B8) {
        for (UINT i = 0; i < r->width * r->height; i++) {
            r->rgba[i * 4 + 0] = r->data[i * 4 + 0];
            r->rgba[i * 4 + 1] = r->data[i * 4 + 1];
            r->rgba[i * 4 + 2] = r->data[i * 4 + 2];
            r->rgba[i * 4 + 3] = 255;
        }
        return;
    }
    if (r->format == D3DFMT_R5G6B5 || r->format == D3DFMT_LIN_R5G6B5 ||
        r->format == D3DFMT_A1R5G5B5 || r->format == D3DFMT_LIN_A1R5G5B5 ||
        r->format == D3DFMT_A4R4G4B4 || r->format == D3DFMT_LIN_A4R4G4B4) {
        const uint16_t *src = (const uint16_t *)r->data;
        uint32_t *dst = (uint32_t *)r->rgba;
        for (UINT i = 0; i < r->width * r->height; i++) {
            uint16_t p = src[i];
            uint8_t a = 255, rr = 0, gg = 0, bb = 0;
            if (r->format == D3DFMT_R5G6B5 || r->format == D3DFMT_LIN_R5G6B5) {
                rr = expand5((p >> 11) & 31); gg = expand6((p >> 5) & 63); bb = expand5(p & 31);
            } else if (r->format == D3DFMT_A1R5G5B5 || r->format == D3DFMT_LIN_A1R5G5B5) {
                a = (p & 0x8000) ? 255 : 0; rr = expand5((p >> 10) & 31); gg = expand5((p >> 5) & 31); bb = expand5(p & 31);
            } else {
                a = expand4((p >> 12) & 15); rr = expand4((p >> 8) & 15); gg = expand4((p >> 4) & 15); bb = expand4(p & 15);
            }
            dst[i] = bgra_color(a, rr, gg, bb);
        }
        return;
    }
    if (r->format == D3DFMT_L8 || r->format == D3DFMT_A8) {
        uint32_t *dst = (uint32_t *)r->rgba;
        for (UINT i = 0; i < r->width * r->height; i++) {
            uint8_t v = r->data[i];
            dst[i] = (r->format == D3DFMT_A8) ? bgra_color(v, 255, 255, 255) : bgra_color(255, v, v, v);
        }
        return;
    }

    if (r->format == D3DFMT_DXT1 || r->format == D3DFMT_DXT3 || r->format == D3DFMT_DXT5) {
        const BYTE *src = r->data;
        uint32_t *dst = (uint32_t *)r->rgba;
        UINT bw = (r->width + 3) / 4;
        UINT bh = (r->height + 3) / 4;
        UINT block_size = (r->format == D3DFMT_DXT1) ? 8 : 16;
        for (UINT by = 0; by < bh; by++) {
            for (UINT bx = 0; bx < bw; bx++) {
                const BYTE *block = src + ((size_t)by * bw + bx) * block_size;
                uint8_t alpha[16];
                for (int i = 0; i < 16; i++) alpha[i] = 255;
                const BYTE *color_block = block;
                if (r->format == D3DFMT_DXT3) {
                    for (int i = 0; i < 16; i++) {
                        uint8_t nibble = (block[i / 2] >> ((i & 1) * 4)) & 15;
                        alpha[i] = expand4(nibble);
                    }
                    color_block = block + 8;
                } else if (r->format == D3DFMT_DXT5) {
                    uint8_t a0 = block[0], a1 = block[1], avals[8];
                    avals[0] = a0; avals[1] = a1;
                    if (a0 > a1) {
                        avals[2] = (uint8_t)((6 * a0 + 1 * a1) / 7);
                        avals[3] = (uint8_t)((5 * a0 + 2 * a1) / 7);
                        avals[4] = (uint8_t)((4 * a0 + 3 * a1) / 7);
                        avals[5] = (uint8_t)((3 * a0 + 4 * a1) / 7);
                        avals[6] = (uint8_t)((2 * a0 + 5 * a1) / 7);
                        avals[7] = (uint8_t)((1 * a0 + 6 * a1) / 7);
                    } else {
                        avals[2] = (uint8_t)((4 * a0 + 1 * a1) / 5);
                        avals[3] = (uint8_t)((3 * a0 + 2 * a1) / 5);
                        avals[4] = (uint8_t)((2 * a0 + 3 * a1) / 5);
                        avals[5] = (uint8_t)((1 * a0 + 4 * a1) / 5);
                        avals[6] = 0; avals[7] = 255;
                    }
                    uint64_t bits = 0;
                    for (int i = 0; i < 6; i++) bits |= (uint64_t)block[2 + i] << (8 * i);
                    for (int i = 0; i < 16; i++) alpha[i] = avals[(bits >> (3 * i)) & 7];
                    color_block = block + 8;
                }

                uint16_t c0 = (uint16_t)color_block[0] | ((uint16_t)color_block[1] << 8);
                uint16_t c1 = (uint16_t)color_block[2] | ((uint16_t)color_block[3] << 8);
                uint8_t cr[4], cg[4], cb[4];
                decode_565(c0, &cr[0], &cg[0], &cb[0]);
                decode_565(c1, &cr[1], &cg[1], &cb[1]);
                if (c0 > c1 || r->format != D3DFMT_DXT1) {
                    cr[2] = (uint8_t)((2 * cr[0] + cr[1]) / 3); cg[2] = (uint8_t)((2 * cg[0] + cg[1]) / 3); cb[2] = (uint8_t)((2 * cb[0] + cb[1]) / 3);
                    cr[3] = (uint8_t)((cr[0] + 2 * cr[1]) / 3); cg[3] = (uint8_t)((cg[0] + 2 * cg[1]) / 3); cb[3] = (uint8_t)((cb[0] + 2 * cb[1]) / 3);
                } else {
                    cr[2] = (uint8_t)((cr[0] + cr[1]) / 2); cg[2] = (uint8_t)((cg[0] + cg[1]) / 2); cb[2] = (uint8_t)((cb[0] + cb[1]) / 2);
                    cr[3] = cg[3] = cb[3] = 0;
                }
                uint32_t indices = (uint32_t)color_block[4] | ((uint32_t)color_block[5] << 8) |
                                   ((uint32_t)color_block[6] << 16) | ((uint32_t)color_block[7] << 24);
                for (UINT py = 0; py < 4; py++) {
                    for (UINT px = 0; px < 4; px++) {
                        UINT x = bx * 4 + px, y = by * 4 + py;
                        if (x >= r->width || y >= r->height) continue;
                        UINT pi = py * 4 + px;
                        UINT ci = (indices >> (2 * pi)) & 3;
                        uint8_t a = (r->format == D3DFMT_DXT1 && c0 <= c1 && ci == 3) ? 0 : alpha[pi];
                        dst[y * r->width + x] = bgra_color(a, cr[ci], cg[ci], cb[ci]);
                    }
                }
            }
        }
    }
}

static NullResource *alloc_resource(size_t iface_size, const void *vtbl, UINT size)
{
    NullResource *r = (NullResource *)calloc(1, sizeof(*r));
    if (!r) return NULL;
    (void)iface_size;
    *(const void **)&r->iface = vtbl;
    r->ref_count = 1;
    r->size = size ? size : 1;
    r->data = (BYTE *)calloc(1, r->size);
    if (!r->data) {
        free(r);
        return NULL;
    }
    return r;
}

static HRESULT __stdcall qi_void(void *self, const IID *riid, void **ppv)
{
    (void)riid;
    if (!ppv) return E_INVALIDARG;
    *ppv = self;
    return S_OK;
}

static ULONG __stdcall res_addref(void *self)
{
    return (ULONG)InterlockedIncrement(&((NullResource *)self)->ref_count);
}

static ULONG __stdcall res_release(void *self)
{
    NullResource *r = (NullResource *)self;
    LONG ref = InterlockedDecrement(&r->ref_count);
    if (ref <= 0) {
        free(r->data);
        free(r->rgba);
        free(r);
    }
    return (ULONG)ref;
}

static HRESULT __stdcall vb_get_device(IDirect3DVertexBuffer8 *self, IDirect3DDevice8 **dev) { (void)self; if (dev) *dev = &g_device; return S_OK; }
static DWORD __stdcall vb_set_priority(IDirect3DVertexBuffer8 *self, DWORD p) { (void)self; return p; }
static DWORD __stdcall vb_get_priority(IDirect3DVertexBuffer8 *self) { (void)self; return 0; }
static void __stdcall vb_preload(IDirect3DVertexBuffer8 *self) { (void)self; }
static DWORD __stdcall vb_get_type(IDirect3DVertexBuffer8 *self) { (void)self; return 3; }
static HRESULT __stdcall vb_lock(IDirect3DVertexBuffer8 *self, UINT off, UINT len, BYTE **out, DWORD flags)
{
    (void)flags;
    NullResource *r = (NullResource *)self;
    if (!out || off >= r->size) return E_INVALIDARG;
    if (len == 0 || off + len > r->size) len = r->size - off;
    (void)len;
    *out = r->data + off;
    return S_OK;
}
static HRESULT __stdcall vb_unlock(IDirect3DVertexBuffer8 *self) { (void)self; return S_OK; }
static HRESULT __stdcall vb_get_desc(IDirect3DVertexBuffer8 *self, void *desc) { (void)self; if (desc) memset(desc, 0, 32); return S_OK; }

static HRESULT __stdcall ib_get_device(IDirect3DIndexBuffer8 *self, IDirect3DDevice8 **dev) { (void)self; if (dev) *dev = &g_device; return S_OK; }
static DWORD __stdcall ib_set_priority(IDirect3DIndexBuffer8 *self, DWORD p) { (void)self; return p; }
static DWORD __stdcall ib_get_priority(IDirect3DIndexBuffer8 *self) { (void)self; return 0; }
static void __stdcall ib_preload(IDirect3DIndexBuffer8 *self) { (void)self; }
static DWORD __stdcall ib_get_type(IDirect3DIndexBuffer8 *self) { (void)self; return 4; }
static HRESULT __stdcall ib_lock(IDirect3DIndexBuffer8 *self, UINT off, UINT len, BYTE **out, DWORD flags) { return vb_lock((IDirect3DVertexBuffer8 *)self, off, len, out, flags); }
static HRESULT __stdcall ib_unlock(IDirect3DIndexBuffer8 *self) { (void)self; return S_OK; }
static HRESULT __stdcall ib_get_desc(IDirect3DIndexBuffer8 *self, void *desc) { (void)self; if (desc) memset(desc, 0, 32); return S_OK; }

static HRESULT __stdcall tex_get_device(IDirect3DTexture8 *self, IDirect3DDevice8 **dev) { (void)self; if (dev) *dev = &g_device; return S_OK; }
static DWORD __stdcall tex_set_priority(IDirect3DTexture8 *self, DWORD p) { (void)self; return p; }
static DWORD __stdcall tex_get_priority(IDirect3DTexture8 *self) { (void)self; return 0; }
static void __stdcall tex_preload(IDirect3DTexture8 *self) { (void)self; }
static DWORD __stdcall tex_get_type(IDirect3DTexture8 *self) { (void)self; return 5; }
static DWORD __stdcall tex_get_level_count(IDirect3DTexture8 *self) { (void)self; return 1; }
static HRESULT __stdcall tex_get_level_desc(IDirect3DTexture8 *self, UINT level, D3DSURFACE_DESC *desc)
{
    NullResource *r = (NullResource *)self;
    if (!desc || level != 0) return E_INVALIDARG;
    memset(desc, 0, sizeof(*desc));
    desc->Width = r->width;
    desc->Height = r->height;
    desc->Format = r->format;
    return S_OK;
}
static HRESULT __stdcall tex_get_surface_level(IDirect3DTexture8 *self, UINT level, IDirect3DSurface8 **out)
{
    (void)self; (void)level;
    if (!out) return E_INVALIDARG;
    NullResource *r = alloc_resource(sizeof(IDirect3DSurface8), &g_surf_vtbl, 1);
    if (!r) return E_OUTOFMEMORY;
    *out = &r->iface.surf;
    return S_OK;
}
static HRESULT __stdcall tex_lock_rect(IDirect3DTexture8 *self, UINT level, D3DLOCKED_RECT *lr, const RECT *rect, DWORD flags)
{
    (void)level; (void)rect; (void)flags;
    NullResource *r = (NullResource *)self;
    if (!lr) return E_INVALIDARG;
    lr->Pitch = (INT)r->pitch;
    lr->pBits = r->data;
    return S_OK;
}
static HRESULT __stdcall tex_unlock_rect(IDirect3DTexture8 *self, UINT level)
{
    (void)level;
    decode_texture((NullResource *)self);
    return S_OK;
}

static HRESULT __stdcall surf_get_device(IDirect3DSurface8 *self, IDirect3DDevice8 **dev) { (void)self; if (dev) *dev = &g_device; return S_OK; }
static HRESULT __stdcall surf_get_desc(IDirect3DSurface8 *self, D3DSURFACE_DESC *desc)
{
    NullResource *r = (NullResource *)self;
    if (!desc) return E_INVALIDARG;
    memset(desc, 0, sizeof(*desc));
    desc->Width = r->width;
    desc->Height = r->height;
    desc->Format = r->format;
    return S_OK;
}
static HRESULT __stdcall surf_lock_rect(IDirect3DSurface8 *self, D3DLOCKED_RECT *lr, const RECT *rect, DWORD flags)
{
    (void)rect; (void)flags;
    NullResource *r = (NullResource *)self;
    if (!lr) return E_INVALIDARG;
    lr->Pitch = (INT)r->pitch;
    lr->pBits = r->data;
    return S_OK;
}
static HRESULT __stdcall surf_unlock_rect(IDirect3DSurface8 *self) { (void)self; return S_OK; }

typedef struct SoftwareVertex {
    float x, y, z, rhw;
    uint32_t color;
    float u, v;
} SoftwareVertex;

typedef struct Source3DVertex {
    float x, y, z;
    float nx, ny, nz;
    uint32_t color;
    float u, v;
} Source3DVertex;

static BOOL make_vertex(const BYTE *base, UINT stride, UINT index, SoftwareVertex *out);
static UINT index_at(const BYTE *indices, D3DFORMAT fmt, UINT idx);

static void matrix_identity(D3DMATRIX *m)
{
    memset(m, 0, sizeof(*m));
    m->_11 = m->_22 = m->_33 = m->_44 = 1.0f;
}

static void matrix_mul(D3DMATRIX *out, const D3DMATRIX *a, const D3DMATRIX *b)
{
    D3DMATRIX tmp;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            tmp.m[i][j] = a->m[i][0] * b->m[0][j] +
                          a->m[i][1] * b->m[1][j] +
                          a->m[i][2] * b->m[2][j] +
                          a->m[i][3] * b->m[3][j];
        }
    }
    *out = tmp;
}

static void matrix_transform(const D3DMATRIX *m, float x, float y, float z,
                             float *ox, float *oy, float *oz, float *ow)
{
    *ox = x * m->_11 + y * m->_21 + z * m->_31 + m->_41;
    *oy = x * m->_12 + y * m->_22 + z * m->_32 + m->_42;
    *oz = x * m->_13 + y * m->_23 + z * m->_33 + m->_43;
    *ow = x * m->_14 + y * m->_24 + z * m->_34 + m->_44;
}

static int ensure_framebuffer(UINT w, UINT h)
{
    if (!w) w = 640;
    if (!h) h = 480;
    if (g_framebuffer && g_fb_width == w && g_fb_height == h) return 1;
    free(g_framebuffer);
    free(g_depthbuffer);
    g_framebuffer = (BYTE *)calloc((size_t)w * h, 4);
    g_depthbuffer = (float *)malloc((size_t)w * h * sizeof(float));
    if (!g_framebuffer || !g_depthbuffer) {
        free(g_framebuffer);
        free(g_depthbuffer);
        g_framebuffer = NULL;
        g_depthbuffer = NULL;
        g_fb_width = g_fb_height = 0;
        return 0;
    }
    g_fb_width = w;
    g_fb_height = h;
    for (UINT i = 0; i < g_fb_width * g_fb_height; i++) g_depthbuffer[i] = 1.0f;
    return 1;
}

static void clear_framebuffer(D3DCOLOR color)
{
    if (!ensure_framebuffer(g_viewport.Width, g_viewport.Height)) return;
    uint32_t *dst = (uint32_t *)g_framebuffer;
    for (UINT i = 0; i < g_fb_width * g_fb_height; i++) dst[i] = color;
}

static void clear_depthbuffer(float z)
{
    if (!ensure_framebuffer(g_viewport.Width, g_viewport.Height) || !g_depthbuffer) return;
    for (UINT i = 0; i < g_fb_width * g_fb_height; i++) g_depthbuffer[i] = z;
}

static void maybe_dump_framebuffer(void)
{
    if (!g_dump_checked) {
        const char *dump = getenv("XBOXRECOMP_DUMP_FRAME");
        g_dump_target_frame = (dump && dump[0]) ? (uint32_t)strtoul(dump, NULL, 10) : 0;
        if (g_dump_target_frame == 0 && dump && dump[0]) g_dump_target_frame = 1;
        g_dump_checked = 1;
    }
    g_dump_present_count++;
    if (!g_dump_target_frame || g_dump_present_count != g_dump_target_frame ||
        !g_framebuffer || !g_fb_width || !g_fb_height) {
        return;
    }

    FILE *f = fopen("/tmp/xboxrecomp_frame.ppm", "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", g_fb_width, g_fb_height);
    for (UINT y = 0; y < g_fb_height; y++) {
        const uint8_t *row = g_framebuffer + (size_t)y * g_fb_width * 4;
        for (UINT x = 0; x < g_fb_width; x++) {
            uint8_t rgb[3] = { row[x * 4 + 2], row[x * 4 + 1], row[x * 4 + 0] };
            fwrite(rgb, 1, sizeof(rgb), f);
        }
    }
    fclose(f);
    fprintf(stderr, "[D3D8/NULL] Dumped frame %u to /tmp/xboxrecomp_frame.ppm\n",
            g_dump_present_count);
}

static uint32_t sample_texture(NullResource *tex, float u, float v)
{
    if (!tex || !tex->rgba || !tex->width || !tex->height) {
        return 0xFFFFFFFFu;
    }
    if (u < 0.0f) u = 0.0f;
    if (v < 0.0f) v = 0.0f;
    if (u > 1.0f) u = 1.0f;
    if (v > 1.0f) v = 1.0f;
    UINT x = (UINT)(u * (float)(tex->width - 1) + 0.5f);
    UINT y = (UINT)(v * (float)(tex->height - 1) + 0.5f);
    return ((const uint32_t *)tex->rgba)[y * tex->width + x];
}

static uint8_t chan_a(uint32_t c) { return (uint8_t)(c >> 24); }
static uint8_t chan_r(uint32_t c) { return (uint8_t)(c >> 16); }
static uint8_t chan_g(uint32_t c) { return (uint8_t)(c >> 8); }
static uint8_t chan_b(uint32_t c) { return (uint8_t)c; }

static uint32_t modulate_color(uint32_t tex, uint32_t diff)
{
    return bgra_color((uint8_t)((chan_a(tex) * chan_a(diff)) / 255),
                      (uint8_t)((chan_r(tex) * chan_r(diff)) / 255),
                      (uint8_t)((chan_g(tex) * chan_g(diff)) / 255),
                      (uint8_t)((chan_b(tex) * chan_b(diff)) / 255));
}

static void blend_pixel(UINT x, UINT y, uint32_t src)
{
    if (x >= g_fb_width || y >= g_fb_height) return;
    uint8_t a = chan_a(src);
    uint32_t *dstp = (uint32_t *)g_framebuffer + y * g_fb_width + x;
    if (a == 255) {
        *dstp = src;
        return;
    }
    if (a == 0) return;
    uint32_t dst = *dstp;
    uint8_t inv = (uint8_t)(255 - a);
    *dstp = bgra_color(255,
        (uint8_t)((chan_r(src) * a + chan_r(dst) * inv) / 255),
        (uint8_t)((chan_g(src) * a + chan_g(dst) * inv) / 255),
        (uint8_t)((chan_b(src) * a + chan_b(dst) * inv) / 255));
}

static void blend_pixel_depth(UINT x, UINT y, float z, uint32_t src)
{
    if (x >= g_fb_width || y >= g_fb_height) return;
    size_t idx = (size_t)y * g_fb_width + x;
    if (g_z_enable && g_depthbuffer) {
        if (z < 0.0f || z > 1.0f || z > g_depthbuffer[idx]) return;
        if (g_z_write_enable) g_depthbuffer[idx] = z;
    }
    blend_pixel(x, y, src);
}

#ifdef XBOXRECOMP_VULKAN_GRAPHICS
static int vulkan_native_enabled(void)
{
    if (!g_vulkan_native_checked) {
        const char *env = getenv("XBOXRECOMP_VULKAN_NATIVE");
        g_vulkan_native_enabled = !(env && env[0] && strcmp(env, "0") == 0);
        if (g_vulkan_native_enabled) {
            fprintf(stderr, "[D3D8/Vulkan] Native RHW draw path enabled\n");
        } else {
            fprintf(stderr, "[D3D8/Vulkan] Native RHW draw path disabled by XBOXRECOMP_VULKAN_NATIVE=0\n");
        }
        g_vulkan_native_checked = 1;
    }
    return g_vulkan_native_enabled;
}

static void vulkan_fill_vertex(D3D8VulkanRhwVertex *dst,
                               const SoftwareVertex *src)
{
    uint32_t color = src->color;
    dst->x = src->x;
    dst->y = src->y;
    dst->z = src->z;
    dst->rhw = src->rhw;
    dst->a = ((color >> 24) & 255) / 255.0f;
    dst->r = ((color >> 16) & 255) / 255.0f;
    dst->g = ((color >> 8) & 255) / 255.0f;
    dst->b = (color & 255) / 255.0f;
    dst->u = src->u;
    dst->v = src->v;
}

static void vulkan_draw_vertices(const D3D8VulkanRhwVertex *verts,
                                 uint32_t vertex_count)
{
    if (!vertex_count || !vulkan_native_enabled()) return;
    NullResource *tex = g_bound_texture[0];
    const void *rgba = tex ? tex->rgba : NULL;
    uint32_t tw = tex ? tex->width : 0;
    uint32_t th = tex ? tex->height : 0;
    d3d8_vulkan_host_draw_rhw(verts, vertex_count, rgba, tw, th,
                              g_z_enable ? 1 : 0,
                              g_z_write_enable ? 1 : 0);
}

static void vulkan_append_triangle(D3D8VulkanRhwVertex *verts,
                                   uint32_t *vertex_count,
                                   const BYTE *vbase, UINT stride,
                                   UINT ia, UINT ib, UINT ic)
{
    SoftwareVertex a, b, c;
    if (!make_vertex(vbase, stride, ia, &a) ||
        !make_vertex(vbase, stride, ib, &b) ||
        !make_vertex(vbase, stride, ic, &c)) {
        return;
    }
    vulkan_fill_vertex(&verts[*vertex_count + 0], &a);
    vulkan_fill_vertex(&verts[*vertex_count + 1], &b);
    vulkan_fill_vertex(&verts[*vertex_count + 2], &c);
    *vertex_count += 3;
}

static void vulkan_draw_primitives_linear(D3DPRIMITIVETYPE type,
                                          const BYTE *vbase, UINT stride,
                                          UINT start_vertex,
                                          UINT primitive_count)
{
    if (!vulkan_native_enabled() || !vbase || !stride || !primitive_count) return;
    if (type != D3DPT_TRIANGLELIST &&
        type != D3DPT_TRIANGLESTRIP &&
        type != D3DPT_TRIANGLEFAN) {
        return;
    }

    D3D8VulkanRhwVertex *verts =
        (D3D8VulkanRhwVertex *)malloc((size_t)primitive_count * 3 * sizeof(*verts));
    if (!verts) return;
    uint32_t vertex_count = 0;
    if (type == D3DPT_TRIANGLELIST) {
        for (UINT i = 0; i < primitive_count; i++) {
            UINT vi = start_vertex + i * 3;
            vulkan_append_triangle(verts, &vertex_count, vbase, stride, vi, vi + 1, vi + 2);
        }
    } else if (type == D3DPT_TRIANGLESTRIP) {
        for (UINT i = 0; i < primitive_count; i++) {
            UINT vi = start_vertex + i;
            if (i & 1) vulkan_append_triangle(verts, &vertex_count, vbase, stride, vi + 1, vi, vi + 2);
            else vulkan_append_triangle(verts, &vertex_count, vbase, stride, vi, vi + 1, vi + 2);
        }
    } else if (type == D3DPT_TRIANGLEFAN) {
        for (UINT i = 0; i < primitive_count; i++) {
            vulkan_append_triangle(verts, &vertex_count, vbase, stride,
                                   start_vertex, start_vertex + i + 1,
                                   start_vertex + i + 2);
        }
    }
    vulkan_draw_vertices(verts, vertex_count);
    free(verts);
}

static void vulkan_draw_primitives_indexed(D3DPRIMITIVETYPE type,
                                           const BYTE *vbase, UINT stride,
                                           const BYTE *indices,
                                           D3DFORMAT fmt, UINT start_index,
                                           UINT primitive_count,
                                           UINT base_vertex)
{
    if (!vulkan_native_enabled() || !vbase || !indices || !stride || !primitive_count) return;
    if (type != D3DPT_TRIANGLELIST && type != D3DPT_TRIANGLESTRIP) return;

    D3D8VulkanRhwVertex *verts =
        (D3D8VulkanRhwVertex *)malloc((size_t)primitive_count * 3 * sizeof(*verts));
    if (!verts) return;
    uint32_t vertex_count = 0;
    if (type == D3DPT_TRIANGLELIST) {
        for (UINT i = 0; i < primitive_count; i++) {
            UINT ii = start_index + i * 3;
            vulkan_append_triangle(verts, &vertex_count, vbase, stride,
                                   base_vertex + index_at(indices, fmt, ii),
                                   base_vertex + index_at(indices, fmt, ii + 1),
                                   base_vertex + index_at(indices, fmt, ii + 2));
        }
    } else if (type == D3DPT_TRIANGLESTRIP) {
        for (UINT i = 0; i < primitive_count; i++) {
            UINT i0 = base_vertex + index_at(indices, fmt, start_index + i);
            UINT i1 = base_vertex + index_at(indices, fmt, start_index + i + 1);
            UINT i2 = base_vertex + index_at(indices, fmt, start_index + i + 2);
            if (i & 1) vulkan_append_triangle(verts, &vertex_count, vbase, stride, i1, i0, i2);
            else vulkan_append_triangle(verts, &vertex_count, vbase, stride, i0, i1, i2);
        }
    }
    vulkan_draw_vertices(verts, vertex_count);
    free(verts);
}
#endif

static float edge_fn(float ax, float ay, float bx, float by, float px, float py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static uint32_t lerp_color(const SoftwareVertex *a, const SoftwareVertex *b,
                           const SoftwareVertex *c, float wa, float wb, float wc)
{
    uint8_t aa = (uint8_t)(chan_a(a->color) * wa + chan_a(b->color) * wb + chan_a(c->color) * wc);
    uint8_t rr = (uint8_t)(chan_r(a->color) * wa + chan_r(b->color) * wb + chan_r(c->color) * wc);
    uint8_t gg = (uint8_t)(chan_g(a->color) * wa + chan_g(b->color) * wb + chan_g(c->color) * wc);
    uint8_t bb = (uint8_t)(chan_b(a->color) * wa + chan_b(b->color) * wb + chan_b(c->color) * wc);
    return bgra_color(aa, rr, gg, bb);
}

static void draw_triangle(const SoftwareVertex *a, const SoftwareVertex *b, const SoftwareVertex *c)
{
    if (!ensure_framebuffer(g_viewport.Width, g_viewport.Height)) return;
    float min_xf = fminf(a->x, fminf(b->x, c->x));
    float max_xf = fmaxf(a->x, fmaxf(b->x, c->x));
    float min_yf = fminf(a->y, fminf(b->y, c->y));
    float max_yf = fmaxf(a->y, fmaxf(b->y, c->y));
    int min_x = (int)floorf(min_xf), max_x = (int)ceilf(max_xf);
    int min_y = (int)floorf(min_yf), max_y = (int)ceilf(max_yf);
    if (max_x < 0 || max_y < 0 || min_x >= (int)g_fb_width || min_y >= (int)g_fb_height) return;
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= (int)g_fb_width) max_x = (int)g_fb_width - 1;
    if (max_y >= (int)g_fb_height) max_y = (int)g_fb_height - 1;

    float area = edge_fn(a->x, a->y, b->x, b->y, c->x, c->y);
    if (area > -0.00001f && area < 0.00001f) return;
    NullResource *tex = g_bound_texture[0];
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float w0 = edge_fn(b->x, b->y, c->x, c->y, px, py) / area;
            float w1 = edge_fn(c->x, c->y, a->x, a->y, px, py) / area;
            float w2 = edge_fn(a->x, a->y, b->x, b->y, px, py) / area;
            if (w0 < -0.0001f || w1 < -0.0001f || w2 < -0.0001f) continue;
            float u = a->u * w0 + b->u * w1 + c->u * w2;
            float v = a->v * w0 + b->v * w1 + c->v * w2;
            float z = a->z * w0 + b->z * w1 + c->z * w2;
            uint32_t color = lerp_color(a, b, c, w0, w1, w2);
            if (tex && tex->rgba) color = modulate_color(sample_texture(tex, u, v), color);
            blend_pixel_depth((UINT)x, (UINT)y, z, color);
        }
    }
}

static BOOL make_vertex_rhw(const BYTE *base, UINT stride, UINT index, SoftwareVertex *out)
{
    if (!base || !out || stride < 16) return FALSE;
    const BYTE *src = base + (size_t)index * stride;
    size_t off = 0;
    memcpy(&out->x, src + off, sizeof(float)); off += 4;
    memcpy(&out->y, src + off, sizeof(float)); off += 4;
    memcpy(&out->z, src + off, sizeof(float)); off += 4;
    memcpy(&out->rhw, src + off, sizeof(float)); off += 4;
    out->color = 0xFFFFFFFFu;
    out->u = 0.0f;
    out->v = 0.0f;
    if (g_current_fvf & D3DFVF_DIFFUSE) {
        if (off + 4 > stride) return FALSE;
        memcpy(&out->color, src + off, sizeof(uint32_t));
        off += 4;
    }
    if (g_current_fvf & D3DFVF_SPECULAR) {
        if (off + 4 > stride) return FALSE;
        off += 4;
    }
    if (((g_current_fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT) > 0) {
        if (off + 8 > stride) return FALSE;
        memcpy(&out->u, src + off, sizeof(float)); off += 4;
        memcpy(&out->v, src + off, sizeof(float));
    }
    return TRUE;
}

static BOOL make_vertex_3d(const BYTE *base, UINT stride, UINT index, SoftwareVertex *out)
{
    if (!base || !out || stride < sizeof(Source3DVertex)) return FALSE;
    const Source3DVertex *src = (const Source3DVertex *)(const void *)(base + (size_t)index * stride);

    D3DMATRIX wv, wvp;
    matrix_mul(&wv, &g_world_matrix, &g_view_matrix);
    matrix_mul(&wvp, &wv, &g_proj_matrix);

    float cx, cy, cz, cw;
    matrix_transform(&wvp, src->x, src->y, src->z, &cx, &cy, &cz, &cw);
    if (cw <= 0.001f) return FALSE;

    float ndc_x = cx / cw;
    float ndc_y = cy / cw;
    float ndc_z = cz / cw;
    if (ndc_x < -4.0f || ndc_x > 4.0f || ndc_y < -4.0f || ndc_y > 4.0f ||
        ndc_z < -0.25f || ndc_z > 1.25f) {
        return FALSE;
    }

    out->x = (float)g_viewport.X + (ndc_x * 0.5f + 0.5f) * (float)g_viewport.Width;
    out->y = (float)g_viewport.Y + (0.5f - ndc_y * 0.5f) * (float)g_viewport.Height;
    out->z = ndc_z;
    out->rhw = 1.0f / cw;
    out->color = src->color ? src->color : 0xFFFFFFFFu;
    out->u = src->u;
    out->v = src->v;
    return TRUE;
}

static BOOL make_vertex(const BYTE *base, UINT stride, UINT index, SoftwareVertex *out)
{
    if (g_current_fvf & D3DFVF_XYZRHW) return make_vertex_rhw(base, stride, index, out);
    if (g_current_fvf & D3DFVF_XYZ) return make_vertex_3d(base, stride, index, out);
    return FALSE;
}

static UINT index_at(const BYTE *indices, D3DFORMAT fmt, UINT idx)
{
    if (fmt == D3DFMT_INDEX32) return ((const uint32_t *)(const void *)indices)[idx];
    return ((const uint16_t *)(const void *)indices)[idx];
}

static void draw_triangle_indices(const BYTE *vbase, UINT stride,
                                  UINT ia, UINT ib, UINT ic)
{
    SoftwareVertex a, b, c;
    if (!make_vertex(vbase, stride, ia, &a) ||
        !make_vertex(vbase, stride, ib, &b) ||
        !make_vertex(vbase, stride, ic, &c)) {
        return;
    }
    draw_triangle(&a, &b, &c);
}

static void draw_primitives_from_indices(D3DPRIMITIVETYPE type, const BYTE *vbase, UINT stride,
                                         const BYTE *indices, D3DFORMAT fmt, UINT start_index,
                                         UINT primitive_count, UINT base_vertex)
{
    if (!vbase || !indices || !stride) return;
#ifdef XBOXRECOMP_VULKAN_GRAPHICS
    vulkan_draw_primitives_indexed(type, vbase, stride, indices, fmt,
                                   start_index, primitive_count, base_vertex);
#endif
    if (type == D3DPT_TRIANGLELIST) {
        for (UINT i = 0; i < primitive_count; i++) {
            UINT ii = start_index + i * 3;
            draw_triangle_indices(vbase, stride,
                                  base_vertex + index_at(indices, fmt, ii),
                                  base_vertex + index_at(indices, fmt, ii + 1),
                                  base_vertex + index_at(indices, fmt, ii + 2));
        }
    } else if (type == D3DPT_TRIANGLESTRIP) {
        for (UINT i = 0; i < primitive_count; i++) {
            UINT i0 = base_vertex + index_at(indices, fmt, start_index + i);
            UINT i1 = base_vertex + index_at(indices, fmt, start_index + i + 1);
            UINT i2 = base_vertex + index_at(indices, fmt, start_index + i + 2);
            if (i & 1) draw_triangle_indices(vbase, stride, i1, i0, i2);
            else draw_triangle_indices(vbase, stride, i0, i1, i2);
        }
    }
}

static HRESULT __stdcall dev_qi(IDirect3DDevice8 *self, const IID *riid, void **ppv) { return qi_void(self, riid, ppv); }
static ULONG __stdcall dev_addref(IDirect3DDevice8 *self) { (void)self; return (ULONG)InterlockedIncrement(&g_device_ref); }
static ULONG __stdcall dev_release(IDirect3DDevice8 *self) { (void)self; return (ULONG)InterlockedDecrement(&g_device_ref); }
static HRESULT __stdcall dev_get_d3d(IDirect3DDevice8 *self, IDirect3D8 **out) { (void)self; if (out) *out = &g_d3d8; return S_OK; }
static HRESULT __stdcall ok0(IDirect3DDevice8 *self) { (void)self; return S_OK; }
static HRESULT __stdcall dev_get_caps(IDirect3DDevice8 *self, void *p) { (void)self; if (p) memset(p, 0, 256); return S_OK; }
static HRESULT __stdcall dev_get_display_mode(IDirect3DDevice8 *self, void *p) { (void)self; if (p) memset(p, 0, 32); return S_OK; }
static HRESULT __stdcall dev_get_creation_params(IDirect3DDevice8 *self, void *p) { (void)self; if (p) memset(p, 0, 32); return S_OK; }
static HRESULT __stdcall dev_reset(IDirect3DDevice8 *self, D3DPRESENT_PARAMETERS *pp) { (void)self; (void)pp; return S_OK; }
static HRESULT __stdcall dev_present(IDirect3DDevice8 *self, const RECT *src, const RECT *dst, HWND wnd, void *dirty)
{
    (void)self; (void)src; (void)dst; (void)wnd; (void)dirty;
    maybe_dump_framebuffer();
#ifdef XBOXRECOMP_VULKAN_GRAPHICS
    d3d8_vulkan_host_present_bgra(g_framebuffer, g_fb_width, g_fb_height, g_fb_width * 4);
#endif
    return S_OK;
}
static HRESULT __stdcall dev_get_backbuffer(IDirect3DDevice8 *self, INT i, DWORD type, IDirect3DSurface8 **out) { (void)self; (void)i; (void)type; return tex_get_surface_level(NULL, 0, out); }
static HRESULT __stdcall dev_clear(IDirect3DDevice8 *self, DWORD c, const D3DRECT *r, DWORD f, D3DCOLOR col, float z, DWORD s)
{
    (void)self; (void)c; (void)r; (void)s;
    if (f & D3DCLEAR_TARGET) clear_framebuffer(col);
    if (f & D3DCLEAR_ZBUFFER) clear_depthbuffer(z);
#ifdef XBOXRECOMP_VULKAN_GRAPHICS
    if (vulkan_native_enabled()) {
        d3d8_vulkan_host_clear(f, col, z, g_viewport.Width, g_viewport.Height);
    }
#endif
    return S_OK;
}
static HRESULT __stdcall dev_set_transform(IDirect3DDevice8 *self, D3DTRANSFORMSTATETYPE st, const D3DMATRIX *m)
{
    (void)self;
    if (!m) return E_INVALIDARG;
    if (st == D3DTS_WORLD) g_world_matrix = *m;
    else if (st == D3DTS_VIEW) g_view_matrix = *m;
    else if (st == D3DTS_PROJECTION) g_proj_matrix = *m;
    return S_OK;
}
static HRESULT __stdcall dev_get_transform(IDirect3DDevice8 *self, D3DTRANSFORMSTATETYPE st, D3DMATRIX *m)
{
    (void)self;
    if (!m) return E_INVALIDARG;
    if (st == D3DTS_WORLD) *m = g_world_matrix;
    else if (st == D3DTS_VIEW) *m = g_view_matrix;
    else if (st == D3DTS_PROJECTION) *m = g_proj_matrix;
    else memset(m, 0, sizeof(*m));
    return S_OK;
}
static HRESULT __stdcall dev_set_rs(IDirect3DDevice8 *self, D3DRENDERSTATETYPE st, DWORD v)
{
    (void)self;
    if (st == D3DRS_ZENABLE) g_z_enable = v ? TRUE : FALSE;
    else if (st == D3DRS_ZWRITEENABLE) g_z_write_enable = v ? TRUE : FALSE;
    else if (st == D3DRS_CULLMODE) g_cull_mode = v;
    return S_OK;
}
static HRESULT __stdcall dev_get_rs(IDirect3DDevice8 *self, D3DRENDERSTATETYPE st, DWORD *v)
{
    (void)self;
    if (!v) return S_OK;
    if (st == D3DRS_ZENABLE) *v = g_z_enable;
    else if (st == D3DRS_ZWRITEENABLE) *v = g_z_write_enable;
    else if (st == D3DRS_CULLMODE) *v = g_cull_mode;
    else *v = 0;
    return S_OK;
}
static HRESULT __stdcall dev_set_tss(IDirect3DDevice8 *self, DWORD s, D3DTEXTURESTAGESTATETYPE t, DWORD v) { (void)self; (void)s; (void)t; (void)v; return S_OK; }
static HRESULT __stdcall dev_get_tss(IDirect3DDevice8 *self, DWORD s, D3DTEXTURESTAGESTATETYPE t, DWORD *v) { (void)self; (void)s; (void)t; if (v) *v = 0; return S_OK; }
static HRESULT __stdcall dev_set_texture(IDirect3DDevice8 *self, DWORD s, IDirect3DBaseTexture8 *t)
{
    (void)self;
    if (s < 4) g_bound_texture[s] = (NullResource *)t;
    return S_OK;
}
static HRESULT __stdcall dev_get_texture(IDirect3DDevice8 *self, DWORD s, IDirect3DBaseTexture8 **t) { (void)self; (void)s; if (t) *t = NULL; return S_OK; }
static HRESULT __stdcall dev_set_stream(IDirect3DDevice8 *self, UINT sn, IDirect3DVertexBuffer8 *vb, UINT stride)
{
    (void)self;
    if (sn == 0) {
        g_stream_vb = (NullResource *)vb;
        g_stream_stride = stride;
    }
    return S_OK;
}
static HRESULT __stdcall dev_get_stream(IDirect3DDevice8 *self, UINT sn, IDirect3DVertexBuffer8 **vb, UINT *stride)
{
    (void)self;
    if (sn == 0) {
        if (vb) *vb = g_stream_vb ? &g_stream_vb->iface.vb : NULL;
        if (stride) *stride = g_stream_stride;
    } else {
        if (vb) *vb = NULL;
        if (stride) *stride = 0;
    }
    return S_OK;
}
static HRESULT __stdcall dev_set_indices(IDirect3DDevice8 *self, IDirect3DIndexBuffer8 *ib, UINT base)
{
    (void)self;
    g_index_ib = (NullResource *)ib;
    g_index_base = base;
    return S_OK;
}
static HRESULT __stdcall dev_get_indices(IDirect3DDevice8 *self, IDirect3DIndexBuffer8 **ib, UINT *base)
{
    (void)self;
    if (ib) *ib = g_index_ib ? &g_index_ib->iface.ib : NULL;
    if (base) *base = g_index_base;
    return S_OK;
}
static HRESULT __stdcall dev_draw(IDirect3DDevice8 *self, D3DPRIMITIVETYPE t, UINT start_vertex, UINT primitive_count)
{
    (void)self;
    if (!g_stream_vb || !g_stream_vb->data || !g_stream_stride) return S_OK;
    const BYTE *base = g_stream_vb->data;
#ifdef XBOXRECOMP_VULKAN_GRAPHICS
    vulkan_draw_primitives_linear(t, base, g_stream_stride,
                                  start_vertex, primitive_count);
#endif
    if (t == D3DPT_TRIANGLELIST) {
        for (UINT i = 0; i < primitive_count; i++) {
            UINT vi = start_vertex + i * 3;
            draw_triangle_indices(base, g_stream_stride, vi, vi + 1, vi + 2);
        }
    } else if (t == D3DPT_TRIANGLESTRIP) {
        for (UINT i = 0; i < primitive_count; i++) {
            UINT vi = start_vertex + i;
            if (i & 1) draw_triangle_indices(base, g_stream_stride, vi + 1, vi, vi + 2);
            else draw_triangle_indices(base, g_stream_stride, vi, vi + 1, vi + 2);
        }
    }
    return S_OK;
}
static HRESULT __stdcall dev_drawi(IDirect3DDevice8 *self, D3DPRIMITIVETYPE t, UINT min_vertex, UINT num_vertices, UINT start_index, UINT primitive_count)
{
    (void)self; (void)min_vertex; (void)num_vertices;
    if (!g_stream_vb || !g_index_ib || !g_stream_vb->data || !g_index_ib->data || !g_stream_stride) return S_OK;
    D3DFORMAT fmt = g_index_ib->format ? g_index_ib->format : D3DFMT_INDEX16;
    draw_primitives_from_indices(t, g_stream_vb->data, g_stream_stride,
                                 g_index_ib->data, fmt, start_index,
                                 primitive_count, g_index_base);
    return S_OK;
}
static HRESULT __stdcall dev_drawup(IDirect3DDevice8 *self, D3DPRIMITIVETYPE t, UINT pc, const void *v, UINT stride)
{
    (void)self;
    if (!v || !stride) return S_OK;
    const BYTE *base = (const BYTE *)v;
#ifdef XBOXRECOMP_VULKAN_GRAPHICS
    vulkan_draw_primitives_linear(t, base, stride, 0, pc);
#endif
    if (t == D3DPT_TRIANGLELIST) {
        for (UINT i = 0; i < pc; i++) draw_triangle_indices(base, stride, i * 3, i * 3 + 1, i * 3 + 2);
    } else if (t == D3DPT_TRIANGLESTRIP) {
        for (UINT i = 0; i < pc; i++) {
            if (i & 1) draw_triangle_indices(base, stride, i + 1, i, i + 2);
            else draw_triangle_indices(base, stride, i, i + 1, i + 2);
        }
    } else if (t == D3DPT_TRIANGLEFAN) {
        for (UINT i = 0; i < pc; i++) draw_triangle_indices(base, stride, 0, i + 1, i + 2);
    }
    return S_OK;
}
static HRESULT __stdcall dev_drawiup(IDirect3DDevice8 *self, D3DPRIMITIVETYPE t, UINT min, UINT nv, UINT pc, const void *idx, D3DFORMAT f, const void *v, UINT stride)
{
    (void)self; (void)min; (void)nv;
    draw_primitives_from_indices(t, (const BYTE *)v, stride, (const BYTE *)idx, f, 0, pc, 0);
    return S_OK;
}

static HRESULT __stdcall dev_create_texture(IDirect3DDevice8 *self, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture8 **out)
{
    (void)self; (void)levels; (void)usage; (void)pool;
    if (!out) return E_INVALIDARG;
    UINT pitch = format_pitch(fmt, w);
    UINT storage = format_storage_size(fmt, w, h ? h : 1);
    UINT rgba_size = w * (h ? h : 1) * 4;
    if (storage < rgba_size) storage = rgba_size;
    NullResource *r = alloc_resource(sizeof(IDirect3DTexture8), &g_tex_vtbl, storage);
    if (!r) return E_OUTOFMEMORY;
    r->width = w; r->height = h; r->pitch = pitch; r->format = fmt;
    r->rgba = (BYTE *)calloc((size_t)w * (h ? h : 1), 4);
    *out = &r->iface.tex;
    return S_OK;
}
static HRESULT __stdcall dev_create_vb(IDirect3DDevice8 *self, UINT len, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer8 **out)
{
    (void)self; (void)usage; (void)fvf; (void)pool;
    if (!out) return E_INVALIDARG;
    NullResource *r = alloc_resource(sizeof(IDirect3DVertexBuffer8), &g_vb_vtbl, len);
    if (!r) return E_OUTOFMEMORY;
    *out = &r->iface.vb;
    return S_OK;
}
static HRESULT __stdcall dev_create_ib(IDirect3DDevice8 *self, UINT len, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DIndexBuffer8 **out)
{
    (void)self; (void)usage; (void)fmt; (void)pool;
    if (!out) return E_INVALIDARG;
    NullResource *r = alloc_resource(sizeof(IDirect3DIndexBuffer8), &g_ib_vtbl, len);
    if (!r) return E_OUTOFMEMORY;
    r->format = fmt;
    *out = &r->iface.ib;
    return S_OK;
}
static HRESULT __stdcall dev_create_surface(IDirect3DDevice8 *self, UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms, BOOL lockable, IDirect3DSurface8 **out)
{
    (void)self; (void)ms; (void)lockable;
    if (!out) return E_INVALIDARG;
    UINT pitch = format_pitch(fmt, w);
    UINT storage = format_storage_size(fmt, w, h ? h : 1);
    NullResource *r = alloc_resource(sizeof(IDirect3DSurface8), &g_surf_vtbl, storage);
    if (!r) return E_OUTOFMEMORY;
    r->width = w; r->height = h; r->pitch = pitch; r->format = fmt;
    *out = &r->iface.surf;
    return S_OK;
}
static HRESULT __stdcall dev_create_depth_surface(IDirect3DDevice8 *self, UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms, IDirect3DSurface8 **out)
{
    return dev_create_surface(self, w, h, fmt, ms, TRUE, out);
}
static HRESULT __stdcall dev_set_rt(IDirect3DDevice8 *self, IDirect3DSurface8 *rt, IDirect3DSurface8 *ds) { (void)self; (void)rt; (void)ds; return S_OK; }
static HRESULT __stdcall dev_get_rt(IDirect3DDevice8 *self, IDirect3DSurface8 **out) { (void)self; return dev_create_surface(self, 640, 480, D3DFMT_A8R8G8B8, 0, TRUE, out); }
static HRESULT __stdcall dev_get_ds(IDirect3DDevice8 *self, IDirect3DSurface8 **out) { (void)self; if (out) *out = NULL; return S_OK; }
static HRESULT __stdcall dev_set_viewport(IDirect3DDevice8 *self, const D3DVIEWPORT8 *vp) { (void)self; if (vp) g_viewport = *vp; return S_OK; }
static HRESULT __stdcall dev_get_viewport(IDirect3DDevice8 *self, D3DVIEWPORT8 *vp) { (void)self; if (vp) *vp = g_viewport; return S_OK; }
static HRESULT __stdcall dev_set_material(IDirect3DDevice8 *self, const D3DMATERIAL8 *m) { (void)self; (void)m; return S_OK; }
static HRESULT __stdcall dev_get_material(IDirect3DDevice8 *self, D3DMATERIAL8 *m) { (void)self; if (m) memset(m, 0, sizeof(*m)); return S_OK; }
static HRESULT __stdcall dev_set_light(IDirect3DDevice8 *self, DWORD i, const D3DLIGHT8 *l) { (void)self; (void)i; (void)l; return S_OK; }
static HRESULT __stdcall dev_get_light(IDirect3DDevice8 *self, DWORD i, D3DLIGHT8 *l) { (void)self; (void)i; if (l) memset(l, 0, sizeof(*l)); return S_OK; }
static HRESULT __stdcall dev_light_enable(IDirect3DDevice8 *self, DWORD i, BOOL e) { (void)self; (void)i; (void)e; return S_OK; }
static HRESULT __stdcall dev_set_shader(IDirect3DDevice8 *self, DWORD h) { (void)self; g_current_fvf = h; return S_OK; }
static HRESULT __stdcall dev_get_shader(IDirect3DDevice8 *self, DWORD *h) { (void)self; if (h) *h = g_current_fvf; return S_OK; }
static HRESULT __stdcall dev_set_const(IDirect3DDevice8 *self, INT r, const void *d, DWORD c) { (void)self; (void)r; (void)d; (void)c; return S_OK; }
static void __stdcall dev_set_gamma(IDirect3DDevice8 *self, DWORD flags, const D3DGAMMARAMP *r) { (void)self; (void)flags; (void)r; }
static void __stdcall dev_get_gamma(IDirect3DDevice8 *self, D3DGAMMARAMP *r) { (void)self; if (r) memset(r, 0, sizeof(*r)); }
static HRESULT __stdcall dev_set_palette(IDirect3DDevice8 *self, DWORD n, const void *e) { (void)self; (void)n; (void)e; return S_OK; }
static HRESULT __stdcall dev_begin_push(IDirect3DDevice8 *self, DWORD count, DWORD **out) { (void)self; if (out) *out = calloc(count ? count : 1, sizeof(DWORD)); return out && *out ? S_OK : E_OUTOFMEMORY; }
static HRESULT __stdcall dev_end_push(IDirect3DDevice8 *self, DWORD *p) { (void)self; free(p); return S_OK; }
static HRESULT __stdcall dev_swap(IDirect3DDevice8 *self, DWORD flags)
{
    (void)self; (void)flags;
    maybe_dump_framebuffer();
#ifdef XBOXRECOMP_VULKAN_GRAPHICS
    d3d8_vulkan_host_present_bgra(g_framebuffer, g_fb_width, g_fb_height, g_fb_width * 4);
#endif
    return S_OK;
}

static HRESULT __stdcall d3d8_qi(IDirect3D8 *self, const IID *riid, void **ppv) { return qi_void(self, riid, ppv); }
static ULONG __stdcall d3d8_addref(IDirect3D8 *self) { (void)self; return (ULONG)InterlockedIncrement(&g_d3d8_ref); }
static ULONG __stdcall d3d8_release(IDirect3D8 *self) { (void)self; return (ULONG)InterlockedDecrement(&g_d3d8_ref); }
static HRESULT __stdcall d3d8_create_device(IDirect3D8 *self, UINT adapter, DWORD type, HWND window, DWORD flags, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice8 **out)
{
    (void)self; (void)adapter; (void)type; (void)window; (void)flags;
    if (pp) {
        g_viewport.Width = pp->BackBufferWidth ? pp->BackBufferWidth : 640;
        g_viewport.Height = pp->BackBufferHeight ? pp->BackBufferHeight : 480;
    }
#ifdef XBOXRECOMP_VULKAN_GRAPHICS
    d3d8_vulkan_host_init(g_viewport.Width, g_viewport.Height);
#endif
    ensure_framebuffer(g_viewport.Width, g_viewport.Height);
    clear_framebuffer(0xFF05070Au);
    clear_depthbuffer(1.0f);
    matrix_identity(&g_world_matrix);
    matrix_identity(&g_view_matrix);
    matrix_identity(&g_proj_matrix);
    g_current_fvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
    g_device.lpVtbl = &g_device_vtbl;
    g_device_ref = 1;
    if (out) *out = &g_device;
    return S_OK;
}

static const IDirect3DVertexBuffer8Vtbl g_vb_vtbl = {
    (void *)qi_void, (void *)res_addref, (void *)res_release,
    vb_get_device, vb_set_priority, vb_get_priority, vb_preload, vb_get_type,
    vb_lock, vb_unlock, vb_get_desc
};
static const IDirect3DIndexBuffer8Vtbl g_ib_vtbl = {
    (void *)qi_void, (void *)res_addref, (void *)res_release,
    ib_get_device, ib_set_priority, ib_get_priority, ib_preload, ib_get_type,
    ib_lock, ib_unlock, ib_get_desc
};
static const IDirect3DTexture8Vtbl g_tex_vtbl = {
    (void *)qi_void, (void *)res_addref, (void *)res_release,
    tex_get_device, tex_set_priority, tex_get_priority, tex_preload, tex_get_type,
    tex_get_level_count, tex_get_level_desc, tex_get_surface_level, tex_lock_rect, tex_unlock_rect
};
static const IDirect3DSurface8Vtbl g_surf_vtbl = {
    (void *)qi_void, (void *)res_addref, (void *)res_release,
    surf_get_device, surf_get_desc, surf_lock_rect, surf_unlock_rect
};
static const IDirect3DDevice8Vtbl g_device_vtbl = {
    dev_qi, dev_addref, dev_release,
    dev_get_d3d, dev_get_caps, dev_get_display_mode, dev_get_creation_params,
    dev_reset, dev_present, dev_get_backbuffer,
    ok0, ok0, dev_clear,
    dev_set_transform, dev_get_transform,
    dev_set_rs, dev_get_rs,
    dev_set_tss, dev_get_tss,
    dev_set_texture, dev_get_texture,
    dev_set_stream, dev_get_stream, dev_set_indices, dev_get_indices,
    dev_draw, dev_drawi, dev_drawup, dev_drawiup,
    dev_create_texture, dev_create_vb, dev_create_ib, dev_create_surface, dev_create_depth_surface,
    dev_set_rt, dev_get_rt, dev_get_ds,
    dev_set_viewport, dev_get_viewport,
    dev_set_material, dev_get_material, dev_set_light, dev_get_light, dev_light_enable,
    dev_set_shader, dev_get_shader, dev_set_const, dev_set_shader, dev_get_shader, dev_set_const,
    dev_set_gamma, dev_get_gamma,
    dev_set_palette,
    dev_begin_push, dev_end_push,
    dev_swap
};
static const IDirect3D8Vtbl g_d3d8_vtbl = {
    d3d8_qi, d3d8_addref, d3d8_release, d3d8_create_device
};

IDirect3D8 *xbox_Direct3DCreate8(UINT SDKVersion)
{
    (void)SDKVersion;
    g_d3d8.lpVtbl = &g_d3d8_vtbl;
    return &g_d3d8;
}

IDirect3DDevice8 *xbox_GetD3DDevice(void)
{
    g_device.lpVtbl = &g_device_vtbl;
    return &g_device;
}

IDirect3DDevice8 *d3d8_GetDevice(void)
{
    return xbox_GetD3DDevice();
}

void d3d8_PresentFrame(void)
{
    maybe_dump_framebuffer();
#ifdef XBOXRECOMP_VULKAN_GRAPHICS
    d3d8_vulkan_host_present_bgra(g_framebuffer, g_fb_width, g_fb_height, g_fb_width * 4);
#endif
}
