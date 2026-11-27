// Local experiment: 2.5D per-pixel lighting pass (see tig/lit25d.h).
//
// Two fragment passes through the SDL GPU API (Metal on macOS, Direct3D 12 on
// Windows) on a private GPU device, like scalefx_gpu.c:
//   1. geometry: per pixel, the ground position of its base and its height,
//      with wall bases interpolated across each wall piece;
//   2. lighting: point lights, sun, shadows, ambient occlusion, glow.
// The lit frame is read back into memory and presented by the game as usual.
//
// Shader sources live in shaders/lit25d/ (Vulkan GLSL); gen.py cross-compiles
// them into lit25d_shaders.h.

#include "tig/lit25d.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>
#endif

#include "lit25d_shaders.h"

static int lit_mode;
static TigLit25dProvider lit_provider;

int tig_lit25d_mode(void)
{
    return lit_mode;
}

void tig_lit25d_set_mode(int mode)
{
    lit_mode = mode;
}

void tig_lit25d_set_provider(TigLit25dProvider provider)
{
    lit_provider = provider;
}

// Matches the std140 LitUniforms block in the shaders.
typedef struct LitUniforms {
    int32_t rect[4];
    float sundir[4];
    float grade[4];
    float params[4]; // ambient, sun, time, night
    int32_t iparams[4]; // origin, mode, number of lights
    float hd[4]; // tile top-left x, y (G-buffer), scale, layers
    float lpos[TIG_LIT25D_MAX_LIGHTS * 4];
    float lcol[TIG_LIT25D_MAX_LIGHTS * 4];
} LitUniforms;

static SDL_GPUDevice* lit_device;
static SDL_GPUGraphicsPipeline* lit_geo_pipeline;
static SDL_GPUGraphicsPipeline* lit_light_pipeline;
static SDL_GPUGraphicsPipeline* lit_hd_pipeline;
static SDL_GPUSampler* lit_linear;
static SDL_GPUTexture* lit_atlas;
static uint8_t* lit_atlas_pending;
static int lit_atlas_w, lit_atlas_h, lit_atlas_layers;
static SDL_GPUTexture* lit_up_tex;
static SDL_GPUTexture* lit_hd_out;
static SDL_GPUTransferBuffer* lit_hd_upbuf;
static SDL_GPUTransferBuffer* lit_hd_downbuf;
static int lit_hd_w, lit_hd_h;
static uint32_t* lit_hd_tmp;
static SDL_GPUSampler* lit_sampler;
static SDL_GPUTexture* lit_color_tex;
static SDL_GPUTexture* lit_out_tex;
static SDL_GPUTexture* lit_gbuf_tex;
static SDL_GPUTexture* lit_geo_tex;
static SDL_GPUTransferBuffer* lit_color_up;
static SDL_GPUTransferBuffer* lit_gbuf_up;
static SDL_GPUTransferBuffer* lit_down;
static int lit_w;
static int lit_h;
static int lit_gw;
static int lit_gh;
static bool lit_ready;
static bool lit_failed;
static TigLit25dFrame lit_frame;
static LitUniforms lit_uniforms;

#if defined(_WIN32)
// HLSL -> DXBC (shader model 5.1) with the compiler every Windows 10+ has.
static void* lit_compile_hlsl(const char* src, const char* name, bool vertex, size_t* out_size)
{
    static pD3DCompile d3d_compile;
    static bool tried;

    if (!tried) {
        tried = true;
        HMODULE lib = LoadLibraryA("d3dcompiler_47.dll");
        if (lib != NULL) {
            d3d_compile = (pD3DCompile)(void*)GetProcAddress(lib, "D3DCompile");
        }
    }
    if (d3d_compile == NULL) {
        SDL_Log("Lit25d: d3dcompiler_47.dll not available");
        return NULL;
    }

    ID3DBlob* code = NULL;
    ID3DBlob* errors = NULL;
    HRESULT hr = d3d_compile(src, strlen(src), name, NULL, NULL, "main",
        vertex ? "vs_5_1" : "ps_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr)) {
        SDL_Log("Lit25d: %s: %s", name,
            errors != NULL ? (const char*)errors->lpVtbl->GetBufferPointer(errors) : "compile failed");
        if (errors != NULL) errors->lpVtbl->Release(errors);
        if (code != NULL) code->lpVtbl->Release(code);
        return NULL;
    }
    if (errors != NULL) errors->lpVtbl->Release(errors);

    *out_size = code->lpVtbl->GetBufferSize(code);
    void* out = malloc(*out_size);
    if (out != NULL) {
        memcpy(out, code->lpVtbl->GetBufferPointer(code), *out_size);
    }
    code->lpVtbl->Release(code);
    return out;
}
#endif

static SDL_GPUShader* lit_shader(const char* name, const char* msl, const char* hlsl, bool vertex, int num_samplers)
{
    SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(lit_device);
    SDL_GPUShaderCreateInfo info;
    void* owned = NULL;

    (void)hlsl;

    SDL_zero(info);
    info.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = (Uint32)num_samplers;
    info.num_uniform_buffers = vertex ? 0 : 1;

    if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0) {
        info.format = SDL_GPU_SHADERFORMAT_MSL;
        info.entrypoint = "main0";
        info.code = (const Uint8*)msl;
        info.code_size = strlen(msl);
#if defined(_WIN32)
    } else if ((formats & SDL_GPU_SHADERFORMAT_DXBC) != 0) {
        size_t size = 0;
        owned = lit_compile_hlsl(hlsl, name, vertex, &size);
        if (owned == NULL) {
            return NULL;
        }
        info.format = SDL_GPU_SHADERFORMAT_DXBC;
        info.entrypoint = "main";
        info.code = (const Uint8*)owned;
        info.code_size = size;
#endif
    } else {
        SDL_Log("Lit25d: no usable shader format (0x%x)", (unsigned)formats);
        return NULL;
    }

    SDL_GPUShader* shader = SDL_CreateGPUShader(lit_device, &info);
    free(owned);
    if (shader == NULL) {
        SDL_Log("Lit25d: %s: %s", name, SDL_GetError());
    }
    return shader;
}

static SDL_GPUGraphicsPipeline* lit_pipeline(SDL_GPUShader* vs, SDL_GPUShader* fs, SDL_GPUTextureFormat format)
{
    SDL_GPUColorTargetDescription target;
    SDL_GPUGraphicsPipelineCreateInfo info;

    SDL_zero(target);
    target.format = format;

    SDL_zero(info);
    info.vertex_shader = vs;
    info.fragment_shader = fs;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    return SDL_CreateGPUGraphicsPipeline(lit_device, &info);
}

static void lit_release(void)
{
    if (lit_device == NULL) {
        return;
    }
    SDL_GPUTexture** textures[] = { &lit_color_tex, &lit_out_tex, &lit_gbuf_tex, &lit_geo_tex };
    for (size_t i = 0; i < SDL_arraysize(textures); i++) {
        if (*textures[i] != NULL) {
            SDL_ReleaseGPUTexture(lit_device, *textures[i]);
            *textures[i] = NULL;
        }
    }
    SDL_GPUTransferBuffer** buffers[] = { &lit_color_up, &lit_gbuf_up, &lit_down };
    for (size_t i = 0; i < SDL_arraysize(buffers); i++) {
        if (*buffers[i] != NULL) {
            SDL_ReleaseGPUTransferBuffer(lit_device, *buffers[i]);
            *buffers[i] = NULL;
        }
    }
    lit_w = lit_h = lit_gw = lit_gh = 0;
}

static SDL_GPUTexture* lit_texture(SDL_GPUTextureFormat format, int w, int h, bool target)
{
    SDL_GPUTextureCreateInfo info;

    SDL_zero(info);
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (target ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
    info.width = (Uint32)w;
    info.height = (Uint32)h;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    return SDL_CreateGPUTexture(lit_device, &info);
}

static SDL_GPUTransferBuffer* lit_transfer(SDL_GPUTransferBufferUsage usage, int bytes)
{
    SDL_GPUTransferBufferCreateInfo info;

    SDL_zero(info);
    info.usage = usage;
    info.size = (Uint32)bytes;
    return SDL_CreateGPUTransferBuffer(lit_device, &info);
}

static bool lit_init(void)
{
    if (lit_ready) {
        return true;
    }
    if (lit_failed) {
        return false;
    }
    lit_failed = true;

    lit_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL
#if defined(_WIN32)
            | SDL_GPU_SHADERFORMAT_DXBC
#endif
        ,
        false,
        NULL);
    if (lit_device == NULL) {
        SDL_Log("Lit25d: no GPU device: %s", SDL_GetError());
        return false;
    }

    SDL_GPUShader* vs = lit_shader("lit25d_vert", lit25d_vert_msl, lit25d_vert_hlsl, true, 0);
    SDL_GPUShader* geo = lit_shader("lit25d_geo", lit25d_geo_msl, lit25d_geo_hlsl, false, 1);
    SDL_GPUShader* light = lit_shader("lit25d_light", lit25d_light_msl, lit25d_light_hlsl, false, 3);
    SDL_GPUShader* hd = lit_shader("lit25d_hd", lit25d_hd_msl, lit25d_hd_hlsl, false, 4);
    bool ok = vs != NULL && geo != NULL && light != NULL && hd != NULL;

    if (ok) {
        lit_geo_pipeline = lit_pipeline(vs, geo, SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT);
        lit_light_pipeline = lit_pipeline(vs, light, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM);
        lit_hd_pipeline = lit_pipeline(vs, hd, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM);
        ok = lit_geo_pipeline != NULL && lit_light_pipeline != NULL && lit_hd_pipeline != NULL;
        if (!ok) {
            SDL_Log("Lit25d: pipeline: %s", SDL_GetError());
        }
    }
    if (vs != NULL) SDL_ReleaseGPUShader(lit_device, vs);
    if (geo != NULL) SDL_ReleaseGPUShader(lit_device, geo);
    if (light != NULL) SDL_ReleaseGPUShader(lit_device, light);
    if (hd != NULL) SDL_ReleaseGPUShader(lit_device, hd);

    if (ok) {
        SDL_GPUSamplerCreateInfo sampler;
        SDL_zero(sampler);
        sampler.min_filter = SDL_GPU_FILTER_NEAREST;
        sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
        sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        lit_sampler = SDL_CreateGPUSampler(lit_device, &sampler);
        sampler.min_filter = SDL_GPU_FILTER_LINEAR;
        sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
        lit_linear = SDL_CreateGPUSampler(lit_device, &sampler);
        ok = lit_sampler != NULL && lit_linear != NULL;
    }

    if (!ok) {
        tig_lit25d_exit();
        return false;
    }

    lit_failed = false;
    lit_ready = true;
    SDL_Log("Lit25d: ready (%s)", SDL_GetGPUDeviceDriver(lit_device));
    return true;
}

static bool lit_targets(int w, int h, int gw, int gh)
{
    if (w != lit_w || h != lit_h) {
        if (lit_color_tex != NULL) SDL_ReleaseGPUTexture(lit_device, lit_color_tex);
        if (lit_out_tex != NULL) SDL_ReleaseGPUTexture(lit_device, lit_out_tex);
        if (lit_color_up != NULL) SDL_ReleaseGPUTransferBuffer(lit_device, lit_color_up);
        if (lit_down != NULL) SDL_ReleaseGPUTransferBuffer(lit_device, lit_down);
        lit_color_tex = lit_texture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, w, h, false);
        lit_out_tex = lit_texture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, w, h, true);
        lit_color_up = lit_transfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, w * h * 4);
        lit_down = lit_transfer(SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, w * h * 4);
        lit_w = w;
        lit_h = h;
    }
    if (gw != lit_gw || gh != lit_gh) {
        if (lit_gbuf_tex != NULL) SDL_ReleaseGPUTexture(lit_device, lit_gbuf_tex);
        if (lit_geo_tex != NULL) SDL_ReleaseGPUTexture(lit_device, lit_geo_tex);
        if (lit_gbuf_up != NULL) SDL_ReleaseGPUTransferBuffer(lit_device, lit_gbuf_up);
        lit_gbuf_tex = lit_texture(SDL_GPU_TEXTUREFORMAT_R32_UINT, gw, gh, false);
        lit_geo_tex = lit_texture(SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, gw, gh, true);
        lit_gbuf_up = lit_transfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, gw * gh * 4);
        lit_gw = gw;
        lit_gh = gh;
    }
    if (lit_color_tex == NULL || lit_out_tex == NULL || lit_color_up == NULL || lit_down == NULL
        || lit_gbuf_tex == NULL || lit_geo_tex == NULL || lit_gbuf_up == NULL) {
        SDL_Log("Lit25d: targets: %s", SDL_GetError());
        lit_release();
        return false;
    }
    return true;
}

static void lit_upload(SDL_GPUCopyPass* copy, SDL_GPUTransferBuffer* tb, SDL_GPUTexture* tex,
    const uint32_t* src, int pitch_px, int w, int h)
{
    uint8_t* up = (uint8_t*)SDL_MapGPUTransferBuffer(lit_device, tb, true);
    if (up == NULL) {
        return;
    }
    for (int y = 0; y < h; y++) {
        memcpy(up + (size_t)y * (size_t)w * 4, src + (size_t)y * (size_t)pitch_px, (size_t)w * 4);
    }
    SDL_UnmapGPUTransferBuffer(lit_device, tb);

    SDL_GPUTextureTransferInfo src_info;
    SDL_GPUTextureRegion region;
    SDL_zero(src_info);
    src_info.transfer_buffer = tb;
    src_info.pixels_per_row = (Uint32)w;
    src_info.rows_per_layer = (Uint32)h;
    SDL_zero(region);
    region.texture = tex;
    region.w = (Uint32)w;
    region.h = (Uint32)h;
    region.d = 1;
    SDL_UploadToGPUTexture(copy, &src_info, &region, true);
}

// Lamps whose height is unknown (z < 0): look for the flame in the frame,
// straight above the light's hot spot. Found close to the hot spot = the hot
// spot is the flame, and the lamp stands ~55 px lower; found higher up = the
// hot spot is the lamp's base. Only lamps with a visible flame get a halo.
static void lit_place_lamps(const uint32_t* src, int pitch_px, int w, int h)
{
    for (int i = 0; i < lit_frame.num_lights && i < TIG_LIT25D_MAX_LIGHTS; i++) {
        TigLit25dLight* l = &lit_frame.lights[i];
        if (l->z >= 0.0f) {
            continue;
        }

        int sx = (int)l->x + lit_frame.x;
        int sy = (int)l->y + lit_frame.y;
        float best = 0.0f;
        int best_k = 0;
        for (int k = -6; k <= 90; k += 2) {
            int y = sy - k;
            if (y < 0 || y >= h) {
                continue;
            }
            for (int dx = -2; dx <= 2; dx++) {
                int x = sx + dx;
                if (x < 0 || x >= w) {
                    continue;
                }
                uint32_t p = src[(size_t)y * (size_t)pitch_px + (size_t)x];
                float luma = ((float)((p >> 16) & 0xFF) * 0.299f
                                 + (float)((p >> 8) & 0xFF) * 0.587f
                                 + (float)(p & 0xFF) * 0.114f)
                    / 255.0f;
                if (luma > best) {
                    best = luma;
                    best_k = k;
                }
            }
        }

        bool flame = best > 0.72f;
        if (flame && best_k >= 14) {
            l->z = (float)best_k;
        } else {
            l->z = 55.0f;
            l->y += 55.0f;
        }
        if (flame) {
            l->flicker += 1.0f; // halo flag, read as alpha >= 1 in the shader
        }
    }
}

static void lit_fill_uniforms(void)
{
    LitUniforms* u = &lit_uniforms;
    int n = lit_frame.num_lights;

    if (n > TIG_LIT25D_MAX_LIGHTS) n = TIG_LIT25D_MAX_LIGHTS;

    memset(u, 0, sizeof(*u));
    u->rect[0] = lit_frame.x;
    u->rect[1] = lit_frame.y;
    u->rect[2] = lit_frame.width;
    u->rect[3] = lit_frame.height;
    u->sundir[0] = lit_frame.sun_dir[0];
    u->sundir[1] = lit_frame.sun_dir[1];
    u->sundir[2] = lit_frame.sun_dir[2];
    u->sundir[3] = lit_frame.sun_reach;
    u->grade[0] = lit_frame.grade[0];
    u->grade[1] = lit_frame.grade[1];
    u->grade[2] = lit_frame.grade[2];
    u->params[0] = lit_frame.ambient;
    u->params[1] = lit_frame.sun;
    u->params[2] = (float)(SDL_GetTicks() % 3600000) / 1000.0f;
    u->params[3] = lit_frame.night;
    u->iparams[0] = lit_frame.origin_y;
    u->iparams[1] = lit_mode;
    u->iparams[2] = n;
    u->hd[0] = (float)lit_frame.tile_x;
    u->hd[1] = (float)lit_frame.tile_y;
    u->hd[2] = 3.0f;
    u->hd[3] = (float)lit_atlas_layers;
    for (int i = 0; i < n; i++) {
        const TigLit25dLight* l = &lit_frame.lights[i];
        u->lpos[i * 4 + 0] = l->x;
        u->lpos[i * 4 + 1] = l->y;
        u->lpos[i * 4 + 2] = l->z;
        u->lpos[i * 4 + 3] = l->radius;
        u->lcol[i * 4 + 0] = l->r;
        u->lcol[i * 4 + 1] = l->g;
        u->lcol[i * 4 + 2] = l->b;
        u->lcol[i * 4 + 3] = l->flicker;
    }
}

bool tig_lit25d_apply(const uint32_t* src, int src_pitch_px, int w, int h, uint32_t* dst, int dst_pitch_px)
{
    if (lit_mode == 0 || lit_provider == NULL) {
        return false;
    }

    memset(&lit_frame, 0, sizeof(lit_frame));
    if (!lit_provider(&lit_frame) || lit_frame.gbuf == NULL || lit_frame.width <= 0 || lit_frame.height <= 0) {
        return false;
    }

    if (!lit_init()) {
        lit_mode = 0;
        return false;
    }

    if (!lit_targets(w, h, lit_frame.width, lit_frame.height)) {
        return false;
    }

    lit_place_lamps(src, src_pitch_px, w, h);
    lit_fill_uniforms();

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(lit_device);
    if (cmd == NULL) {
        return false;
    }

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    lit_upload(copy, lit_color_up, lit_color_tex, src, src_pitch_px, w, h);
    lit_upload(copy, lit_gbuf_up, lit_gbuf_tex, lit_frame.gbuf, lit_frame.gbuf_pitch, lit_frame.width, lit_frame.height);
    SDL_EndGPUCopyPass(copy);

    // Pass 1: geometry.
    {
        SDL_GPUColorTargetInfo target;
        SDL_zero(target);
        target.texture = lit_geo_tex;
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_PushGPUFragmentUniformData(cmd, 0, &lit_uniforms, sizeof(lit_uniforms));
        SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &target, 1, NULL);
        SDL_BindGPUGraphicsPipeline(rp, lit_geo_pipeline);
        SDL_GPUTextureSamplerBinding bind = { lit_gbuf_tex, lit_sampler };
        SDL_BindGPUFragmentSamplers(rp, 0, &bind, 1);
        SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
        SDL_EndGPURenderPass(rp);
    }

    // Pass 2: lighting.
    {
        SDL_GPUColorTargetInfo target;
        SDL_zero(target);
        target.texture = lit_out_tex;
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_PushGPUFragmentUniformData(cmd, 0, &lit_uniforms, sizeof(lit_uniforms));
        SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &target, 1, NULL);
        SDL_BindGPUGraphicsPipeline(rp, lit_light_pipeline);
        SDL_GPUTextureSamplerBinding bind[3] = {
            { lit_color_tex, lit_sampler },
            { lit_gbuf_tex, lit_sampler },
            { lit_geo_tex, lit_sampler },
        };
        SDL_BindGPUFragmentSamplers(rp, 0, bind, 3);
        SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
        SDL_EndGPURenderPass(rp);
    }

    copy = SDL_BeginGPUCopyPass(cmd);
    {
        SDL_GPUTextureRegion region;
        SDL_GPUTextureTransferInfo dst_info;
        SDL_zero(region);
        region.texture = lit_out_tex;
        region.w = (Uint32)w;
        region.h = (Uint32)h;
        region.d = 1;
        SDL_zero(dst_info);
        dst_info.transfer_buffer = lit_down;
        dst_info.pixels_per_row = (Uint32)w;
        dst_info.rows_per_layer = (Uint32)h;
        SDL_DownloadFromGPUTexture(copy, &region, &dst_info);
    }
    SDL_EndGPUCopyPass(copy);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == NULL) {
        return false;
    }
    SDL_WaitForGPUFences(lit_device, true, &fence, 1);
    SDL_ReleaseGPUFence(lit_device, fence);

    const uint8_t* down = (const uint8_t*)SDL_MapGPUTransferBuffer(lit_device, lit_down, false);
    if (down == NULL) {
        return false;
    }
    for (int y = 0; y < h; y++) {
        memcpy(dst + (size_t)y * (size_t)dst_pitch_px, down + (size_t)y * (size_t)w * 4, (size_t)w * 4);
    }
    SDL_UnmapGPUTransferBuffer(lit_device, lit_down);
    return true;
}

void tig_lit25d_exit(void)
{
    if (lit_device == NULL) {
        return;
    }
    lit_release();
    if (lit_geo_pipeline != NULL) {
        SDL_ReleaseGPUGraphicsPipeline(lit_device, lit_geo_pipeline);
        lit_geo_pipeline = NULL;
    }
    if (lit_light_pipeline != NULL) {
        SDL_ReleaseGPUGraphicsPipeline(lit_device, lit_light_pipeline);
        lit_light_pipeline = NULL;
    }
    if (lit_sampler != NULL) {
        SDL_ReleaseGPUSampler(lit_device, lit_sampler);
        lit_sampler = NULL;
    }
    if (lit_linear != NULL) {
        SDL_ReleaseGPUSampler(lit_device, lit_linear);
        lit_linear = NULL;
    }
    if (lit_hd_pipeline != NULL) {
        SDL_ReleaseGPUGraphicsPipeline(lit_device, lit_hd_pipeline);
        lit_hd_pipeline = NULL;
    }
    SDL_GPUTexture** hd_textures[] = { &lit_atlas, &lit_up_tex, &lit_hd_out };
    for (size_t i = 0; i < SDL_arraysize(hd_textures); i++) {
        if (*hd_textures[i] != NULL) {
            SDL_ReleaseGPUTexture(lit_device, *hd_textures[i]);
            *hd_textures[i] = NULL;
        }
    }
    if (lit_hd_upbuf != NULL) SDL_ReleaseGPUTransferBuffer(lit_device, lit_hd_upbuf);
    if (lit_hd_downbuf != NULL) SDL_ReleaseGPUTransferBuffer(lit_device, lit_hd_downbuf);
    lit_hd_upbuf = lit_hd_downbuf = NULL;
    lit_hd_w = lit_hd_h = 0;
    SDL_DestroyGPUDevice(lit_device);
    lit_device = NULL;
    lit_ready = false;
}

void tig_lit25d_set_hd_atlas(const uint8_t* rgba, int w, int h, int layers)
{
    SDL_free(lit_atlas_pending);
    lit_atlas_pending = NULL;
    lit_atlas_layers = 0;
    if (rgba == NULL || w <= 0 || h <= 0 || layers <= 0) {
        return;
    }
    size_t size = (size_t)w * (size_t)h * 4 * (size_t)layers;
    lit_atlas_pending = (uint8_t*)SDL_malloc(size);
    if (lit_atlas_pending == NULL) {
        return;
    }
    SDL_memcpy(lit_atlas_pending, rgba, size);
    lit_atlas_w = w;
    lit_atlas_h = h;
    lit_atlas_layers = layers;
}

// Uploads the pending atlas (once), layer by layer.
static bool lit_hd_atlas_ready(void)
{
    if (lit_atlas != NULL) {
        return true;
    }
    if (lit_atlas_pending == NULL) {
        return false;
    }

    SDL_GPUTextureCreateInfo info;
    SDL_zero(info);
    info.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = (Uint32)lit_atlas_w;
    info.height = (Uint32)lit_atlas_h;
    info.layer_count_or_depth = (Uint32)lit_atlas_layers;
    info.num_levels = 1;
    lit_atlas = SDL_CreateGPUTexture(lit_device, &info);
    if (lit_atlas == NULL) {
        SDL_Log("Lit25d: atlas: %s", SDL_GetError());
        return false;
    }

    size_t layer_size = (size_t)lit_atlas_w * (size_t)lit_atlas_h * 4;
    SDL_GPUTransferBuffer* tb = lit_transfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, (int)layer_size);
    if (tb == NULL) {
        return false;
    }
    for (int layer = 0; layer < lit_atlas_layers; layer++) {
        uint8_t* up = (uint8_t*)SDL_MapGPUTransferBuffer(lit_device, tb, true);
        if (up == NULL) {
            break;
        }
        SDL_memcpy(up, lit_atlas_pending + layer_size * (size_t)layer, layer_size);
        SDL_UnmapGPUTransferBuffer(lit_device, tb);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(lit_device);
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo src;
        SDL_GPUTextureRegion region;
        SDL_zero(src);
        src.transfer_buffer = tb;
        src.pixels_per_row = (Uint32)lit_atlas_w;
        src.rows_per_layer = (Uint32)lit_atlas_h;
        SDL_zero(region);
        region.texture = lit_atlas;
        region.layer = (Uint32)layer;
        region.w = (Uint32)lit_atlas_w;
        region.h = (Uint32)lit_atlas_h;
        region.d = 1;
        SDL_UploadToGPUTexture(copy, &src, &region, false);
        SDL_EndGPUCopyPass(copy);
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (fence != NULL) {
            SDL_WaitForGPUFences(lit_device, true, &fence, 1);
            SDL_ReleaseGPUFence(lit_device, fence);
        }
    }
    SDL_ReleaseGPUTransferBuffer(lit_device, tb);
    SDL_free(lit_atlas_pending);
    lit_atlas_pending = NULL;
    SDL_Log("Lit25d: HD atlas %dx%d x %d tiles", lit_atlas_w, lit_atlas_h, lit_atlas_layers);
    return true;
}

typedef struct LitKeepRect {
    int x, y, width, height;
} LitKeepRect;

void tig_lit25d_hd_apply(uint32_t* frame, int pitch_px, int w, int h, int scale, const void* keep_rects, int num_keep_rects)
{
    if (!lit_ready || lit_mode == 0 || lit_atlas_layers == 0 || scale != 3
        || lit_out_tex == NULL || w != lit_w * scale || h != lit_h * scale) {
        return;
    }
    if (!lit_hd_atlas_ready()) {
        return;
    }

    if (w != lit_hd_w || h != lit_hd_h) {
        if (lit_up_tex != NULL) SDL_ReleaseGPUTexture(lit_device, lit_up_tex);
        if (lit_hd_out != NULL) SDL_ReleaseGPUTexture(lit_device, lit_hd_out);
        if (lit_hd_upbuf != NULL) SDL_ReleaseGPUTransferBuffer(lit_device, lit_hd_upbuf);
        if (lit_hd_downbuf != NULL) SDL_ReleaseGPUTransferBuffer(lit_device, lit_hd_downbuf);
        SDL_free(lit_hd_tmp);
        lit_up_tex = lit_texture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, w, h, false);
        lit_hd_out = lit_texture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, w, h, true);
        lit_hd_upbuf = lit_transfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, w * h * 4);
        lit_hd_downbuf = lit_transfer(SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, w * h * 4);
        lit_hd_tmp = (uint32_t*)SDL_malloc((size_t)w * (size_t)h * 4);
        lit_hd_w = w;
        lit_hd_h = h;
    }
    if (lit_up_tex == NULL || lit_hd_out == NULL || lit_hd_upbuf == NULL || lit_hd_downbuf == NULL || lit_hd_tmp == NULL) {
        return;
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(lit_device);
    if (cmd == NULL) {
        return;
    }
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    lit_upload(copy, lit_hd_upbuf, lit_up_tex, frame, pitch_px, w, h);
    SDL_EndGPUCopyPass(copy);

    SDL_GPUColorTargetInfo target;
    SDL_zero(target);
    target.texture = lit_hd_out;
    target.load_op = SDL_GPU_LOADOP_DONT_CARE;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_PushGPUFragmentUniformData(cmd, 0, &lit_uniforms, sizeof(lit_uniforms));
    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &target, 1, NULL);
    SDL_BindGPUGraphicsPipeline(rp, lit_hd_pipeline);
    SDL_GPUTextureSamplerBinding bind[4] = {
        { lit_up_tex, lit_sampler },
        { lit_out_tex, lit_linear },
        { lit_gbuf_tex, lit_sampler },
        { lit_atlas, lit_linear },
    };
    SDL_BindGPUFragmentSamplers(rp, 0, bind, 4);
    SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
    SDL_EndGPURenderPass(rp);

    copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion region;
    SDL_GPUTextureTransferInfo dst_info;
    SDL_zero(region);
    region.texture = lit_hd_out;
    region.w = (Uint32)w;
    region.h = (Uint32)h;
    region.d = 1;
    SDL_zero(dst_info);
    dst_info.transfer_buffer = lit_hd_downbuf;
    dst_info.pixels_per_row = (Uint32)w;
    dst_info.rows_per_layer = (Uint32)h;
    SDL_DownloadFromGPUTexture(copy, &region, &dst_info);
    SDL_EndGPUCopyPass(copy);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == NULL) {
        return;
    }
    SDL_WaitForGPUFences(lit_device, true, &fence, 1);
    SDL_ReleaseGPUFence(lit_device, fence);

    const uint32_t* down = (const uint32_t*)SDL_MapGPUTransferBuffer(lit_device, lit_hd_downbuf, false);
    if (down == NULL) {
        return;
    }

    // Keep the interface: remember what is under the keep rects, write the
    // whole frame, then put those pixels back.
    const LitKeepRect* keep = (const LitKeepRect*)keep_rects;
    for (int y = 0; y < h; y++) {
        uint32_t* row = frame + (size_t)y * (size_t)pitch_px;
        const uint32_t* src = down + (size_t)y * (size_t)w;
        int gy = y / scale;
        int x = 0;
        while (x < w) {
            int gx = x / scale;
            int skip_to = -1;
            for (int r = 0; r < num_keep_rects; r++) {
                if (gx >= keep[r].x && gx < keep[r].x + keep[r].width
                    && gy >= keep[r].y && gy < keep[r].y + keep[r].height) {
                    skip_to = (keep[r].x + keep[r].width) * scale;
                    break;
                }
            }
            if (skip_to > x) {
                x = skip_to;
                continue;
            }
            row[x] = src[x] | 0xFF000000u;
            x++;
        }
    }
    SDL_UnmapGPUTransferBuffer(lit_device, lit_hd_downbuf);
    (void)lit_hd_tmp;
}
