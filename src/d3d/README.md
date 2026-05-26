# xbox_d3d8 — Xbox D3D8 Compatibility

Implements the Xbox's modified Direct3D 8 interface. On Windows this is backed by a Direct3D 11 device. On Linux the default backend uses SDL2 and Vulkan while preserving the same D3D8 ABI.

## Files

| File | LOC | Purpose |
|------|-----|---------|
| `d3d8_xbox.h` | 716 | Public header — all D3D8 types, enums, COM interfaces |
| `d3d8_internal.h` | 129 | Internal wrapper structs (D3D8 interface → D3D11 resource) |
| `d3d8_device.c` | 1,107 | Device creation, render state, draw calls, frame present |
| `d3d8_resources.c` | 541 | Vertex/index buffers, textures, format conversion |
| `d3d8_shaders.c` | 529 | Shader compilation, input layout, constant buffers |
| `d3d8_states.c` | 350 | Render state translation (D3D8 → D3D11), sampler states |
| `d3d8_null.c` | 1,000+ | Portable ABI backend, software fallback rasterizer, texture decode, and Vulkan RHW bridge |
| `d3d8_vulkan_host.c` | 1,000+ | Linux SDL/Vulkan host, swapchain, render target, depth state, texture upload, and RHW draw submission |
| `shaders/d3d8_rhw.vert` | - | Vulkan vertex shader for pre-transformed `XYZRHW` geometry |
| `shaders/d3d8_rhw.frag` | - | Vulkan fragment shader for vertex color and optional texture sampling |

## Quick Start

```c
#include "d3d8_xbox.h"

// Create D3D8 interface (D3D11 on Windows, Vulkan/null backend on Linux)
IDirect3D8 *d3d = xbox_Direct3DCreate8(0);

// Create device (creates window, swap chain, render targets)
IDirect3DDevice8 *dev;
D3DPRESENT_PARAMETERS pp = { .BackBufferWidth = 640, .BackBufferHeight = 480 };
d3d->lpVtbl->CreateDevice(d3d, 0, 0, hwnd, 0, &pp, &dev);

// Standard D3D8 rendering
dev->lpVtbl->BeginScene(dev);
dev->lpVtbl->SetRenderState(dev, D3DRS_ZENABLE, TRUE);
dev->lpVtbl->SetTexture(dev, 0, texture);
dev->lpVtbl->DrawPrimitive(dev, D3DPT_TRIANGLELIST, 0, tri_count);
dev->lpVtbl->EndScene(dev);

// Present
d3d8_PresentFrame();
```

## Linux Status

The Linux Vulkan backend owns the SDL window, Vulkan instance/device/swapchain, render target, and depth image. It now has an initial native fixed-function path for pre-transformed `XYZRHW` geometry, including texture upload, depth clear/test/write support, and whole-call triangle batching. This is enough for menu/UI geometry and other already-transformed draw streams seen in early bring-up.

The backend is still intentionally narrow. Full NV2A push-buffer lowering, programmable vertex shader lowering, register-combiner parity, and untransformed fixed-function lighting are still active work. The `null` backend remains useful for fast headless kernel/recomp debugging and software framebuffer dumps.

Useful Linux runtime switches:

```bash
XBOXRECOMP_DUMP_FRAME=1 ./your_game
XBOXRECOMP_DUMP_VULKAN_FRAME=1 ./your_game
XBOXRECOMP_VULKAN_NATIVE=0 ./your_game
```

## How It Works

The layer maintains two parallel states:

1. **D3D8 state** — what the game thinks is set (render states, textures, transforms)
2. **D3D11 state** — the actual GPU state (constant buffers, shader resource views, etc.)

On each draw call, dirty D3D8 state is flushed to D3D11:

```
Game calls SetRenderState(D3DRS_ZENABLE, TRUE)
  → Stores in d3d8_render_state[D3DRS_ZENABLE]
  → Marks depth state dirty

Game calls DrawPrimitive(...)
  → If depth dirty: create ID3D11DepthStencilState, bind it
  → If blend dirty: create ID3D11BlendState, bind it
  → If raster dirty: create ID3D11RasterizerState, bind it
  → Upload transform matrices to constant buffer
  → Issue ID3D11DeviceContext::Draw()
```

## Supported Features

### Render States (D3DRENDERSTATETYPE)

| Category | States | Status |
|----------|--------|--------|
| Depth | ZENABLE, ZWRITEENABLE, ZFUNC | Implemented |
| Blending | ALPHABLENDENABLE, SRCBLEND, DESTBLEND, BLENDOP | Implemented |
| Alpha test | ALPHATESTENABLE, ALPHAREF, ALPHAFUNC | Implemented |
| Culling | CULLMODE | Implemented |
| Fill | FILLMODE | Implemented |
| Fog | FOGENABLE, FOGCOLOR, FOGTABLEMODE, FOGSTART, FOGEND | Implemented |
| Stencil | STENCILENABLE, STENCILFUNC, STENCILREF, etc. | Implemented |
| Lighting | LIGHTING, AMBIENT | Partial |
| Xbox pixel shaders | PSALPHAINPUTS0-7, PSFINALCOMBINER* | Stubbed |

### Texture Stage States (D3DTEXTURESTAGESTATETYPE)

```c
D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_COLORARG2  // Color combine
D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2  // Alpha combine
D3DTSS_ADDRESSU, D3DTSS_ADDRESSV                      // Wrap modes
D3DTSS_MAGFILTER, D3DTSS_MINFILTER, D3DTSS_MIPFILTER  // Filtering
D3DTSS_MIPMAPLODBIAS, D3DTSS_MAXMIPLEVEL              // LOD control
D3DTSS_TEXCOORDINDEX                                   // UV set selection
D3DTSS_BORDERCOLOR                                     // Border color
```

### Texture Formats (D3DFORMAT)

| Format | Code | Notes |
|--------|------|-------|
| D3DFMT_A8R8G8B8 | 0x06 | Standard ARGB |
| D3DFMT_X8R8G8B8 | 0x07 | Opaque RGB |
| D3DFMT_R5G6B5 | 0x05 | 16-bit RGB |
| D3DFMT_A4R4G4B4 | 0x04 | 16-bit ARGB |
| D3DFMT_A1R5G5B5 | 0x02 | 16-bit with 1-bit alpha |
| D3DFMT_DXT1 | 0x0C | BC1 compressed (4:1) |
| D3DFMT_DXT3 | 0x0E | BC2 compressed (explicit alpha) |
| D3DFMT_DXT5 | 0x0F | BC3 compressed (interpolated alpha) |
| D3DFMT_LIN_A8R8G8B8 | 0x12 | Linear (unswizzled) ARGB |
| D3DFMT_LIN_X8R8G8B8 | 0x1E | Linear opaque RGB |
| D3DFMT_D16 | 0x2C | 16-bit depth |
| D3DFMT_D24S8 | 0x2A | 24-bit depth + 8-bit stencil |
| D3DFMT_YUY2 | 0x24 | YUV packed (video frames) |
| D3DFMT_P8 | 0x0B | 8-bit palettized |

### Primitive Types

```c
D3DPT_POINTLIST      // Individual points
D3DPT_LINELIST       // Line pairs
D3DPT_LINESTRIP      // Connected lines
D3DPT_TRIANGLELIST   // Triangle triples
D3DPT_TRIANGLESTRIP  // Connected triangles
D3DPT_TRIANGLEFAN    // Fan triangles
D3DPT_QUADLIST       // Xbox-specific: quad pairs (split into 2 tris each)
```

### Flexible Vertex Format (FVF)

```c
D3DFVF_XYZ           // float3 position (transformed by MVP)
D3DFVF_XYZRHW        // float4 pre-transformed position (screen space)
D3DFVF_NORMAL         // float3 normal vector
D3DFVF_DIFFUSE        // DWORD diffuse color (ARGB)
D3DFVF_SPECULAR       // DWORD specular color
D3DFVF_TEX0 - TEX4    // 0-4 texture coordinate sets (float2 each)
```

### Device Methods (70+ in IDirect3DDevice8Vtbl)

Key methods:

```c
// Scene management
BeginScene(), EndScene(), Clear()

// Transforms (VIEW, PROJECTION, WORLD, TEXTURE0-3)
SetTransform(type, &matrix), GetTransform(type, &matrix)

// Drawing
DrawPrimitive(type, start_vertex, prim_count)
DrawIndexedPrimitive(type, min_idx, num_verts, start_idx, prim_count)
DrawPrimitiveUP(type, prim_count, vertex_data, stride)
DrawIndexedPrimitiveUP(type, min_idx, num_verts, prim_count, idx_data, idx_fmt, vtx_data, stride)

// Resources
CreateTexture(w, h, levels, usage, fmt, pool, &texture)
CreateVertexBuffer(length, usage, fvf, pool, &buffer)
CreateIndexBuffer(length, usage, fmt, pool, &buffer)
SetTexture(stage, texture)
SetStreamSource(stream, buffer, stride)
SetIndices(buffer, base_vertex_index)

// State
SetRenderState(state, value)
SetTextureStageState(stage, type, value)
SetVertexShader(fvf_or_handle)
SetPixelShader(handle)

// Viewport
SetViewport(&viewport)

// Render targets
SetRenderTarget(surface, depth_surface)
CreateRenderTarget(w, h, fmt, multisample, lockable, &surface)
CreateDepthStencilSurface(w, h, fmt, multisample, &surface)

// Xbox extensions
BeginPush(count, &push_ptr)   // Direct push buffer access
EndPush(push_ptr)
Swap(flags)                    // Xbox-style present
```

## Shaders

The D3D8 layer compiles fixed-function vertex and pixel shaders at startup:

- **Vertex shader**: Transforms position by WVP matrix, passes through color/texcoords
- **Pixel shader**: Samples texture, modulates with vertex color, applies fog

Xbox games that use custom vertex/pixel shader programs (via push buffer microcode) require the NV2A library for proper emulation.

## Xbox-Specific Differences from PC D3D8

1. **Swizzled textures** — Xbox textures use Morton-code (Z-order) swizzling. Linear formats are prefixed with `LIN_`.
2. **Push buffers** — Games can write GPU commands directly via `BeginPush`/`EndPush`.
3. **Pixel shader combiners** — Xbox uses register combiners instead of pixel shaders. Controlled via `D3DRS_PSALPHAINPUTS*` and `D3DRS_PSFINALCOMBINER*` render states.
4. **No shader model** — No vs_1_1/ps_1_1. Vertex programs are NV2A microcode, pixel processing uses fixed-function combiners.
5. **Tile memory** — Xbox has tile-based render targets. Abstracted away by this layer.
