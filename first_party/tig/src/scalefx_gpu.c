// Local tweak: ScaleFX 3x upscale through the SDL GPU API.
//
// Same job as scalefx_gl.c, but on whatever SDL GPU picks: Direct3D 12 on
// Windows, Metal on macOS. Runs the five ScaleFX passes by Sp00kyFox (MIT,
// cross-compiled from the libretro GLSL by shaders/scalefx/gpu/gen.py) on a
// private GPU device and reads the 3x result back into memory, so the game
// keeps presenting through its SDL renderer as before.
//
// Two ways to use it: tig_scalefx_gpu_run() on a private device, reading the
// 3x frame back into memory (works under any SDL renderer, but moving ~33 MB
// per frame each way is slow on many PCs); or, when the SDL renderer itself
// is the "gpu" one, tig_scalefx_gpu_attach() + tig_scalefx_gpu_render() run
// the passes on the renderer's device and hand back the GPU texture to draw,
// with no readback at all.
//
// Shaders are loaded at runtime from HighRes/shaders/: scalefx-*.msl for
// Metal, scalefx-*.hlsl for Direct3D 12 (compiled to DXBC with the system's
// d3dcompiler_47.dll), scalefx-*.spv for Vulkan.

#include "tig/scalefx_gl.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>
#endif

#define SFX_PASSES 5

typedef struct SfxUniforms {
    float texture_size[2];
    float input_size[2];
    float output_size[2];
    int frame_count;
    int frame_direction;
} SfxUniforms;

typedef struct SfxGpuPass {
    SDL_GPUGraphicsPipeline* pipeline;
    SDL_GPUTexture* tex;
    int width;
    int height;
} SfxGpuPass;

// Per pass: output scale, output format, number of sampled textures (the
// second one is PassPrev2 for pass 2 and PassPrev5 = the source for pass 4).
static const int sfx_scale[SFX_PASSES] = { 1, 1, 1, 1, 3 };
static const int sfx_samplers[SFX_PASSES] = { 1, 1, 2, 1, 2 };
static const SDL_GPUTextureFormat sfx_format[SFX_PASSES] = {
    SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
    SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
    SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
    SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
    SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
};

static SDL_GPUDevice* sfx_device;
static SDL_GPUSampler* sfx_sampler;
static SfxGpuPass sfx_passes[SFX_PASSES];
static SDL_GPUTexture* sfx_src_tex;
static SDL_GPUTransferBuffer* sfx_upload;
static SDL_GPUTransferBuffer* sfx_download;
static int sfx_w;
static int sfx_h;
static bool sfx_ready;
static bool sfx_failed;
// False when the device belongs to the SDL renderer (direct path).
static bool sfx_own_device;

static char* read_file(const char* path, size_t* size_ptr)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)size + 1);
    size_t n = 0;
    if (buf != NULL) {
        n = fread(buf, 1, (size_t)size, f);
        buf[n] = '\0';
    }
    fclose(f);
    *size_ptr = n;
    return buf;
}

#if defined(_WIN32)
// HLSL -> DXBC (shader model 5.1) with the compiler every Windows 10+ has.
static void* compile_hlsl(const char* src, size_t size, const char* name, bool vertex, size_t* out_size)
{
    static pD3DCompile d3d_compile;
    static bool tried;

    if (!tried) {
        tried = true;
        HMODULE lib = LoadLibraryA("d3dcompiler_47.dll");
        if (lib != NULL) {
            d3d_compile = (pD3DCompile)(void*)GetProcAddress(lib, "D3DCompile");
        }
        if (d3d_compile == NULL) {
            SDL_Log("ScaleFX GPU: d3dcompiler_47.dll not available");
        }
    }
    if (d3d_compile == NULL) {
        return NULL;
    }

    ID3DBlob* code = NULL;
    ID3DBlob* errors = NULL;
    HRESULT hr = d3d_compile(src, size, name, NULL, NULL, "main",
        vertex ? "vs_5_1" : "ps_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr)) {
        SDL_Log("ScaleFX GPU: %s: %s", name,
            errors != NULL ? (const char*)errors->lpVtbl->GetBufferPointer(errors) : "compile failed");
        if (errors != NULL) {
            errors->lpVtbl->Release(errors);
        }
        if (code != NULL) {
            code->lpVtbl->Release(code);
        }
        return NULL;
    }
    if (errors != NULL) {
        errors->lpVtbl->Release(errors);
    }

    *out_size = code->lpVtbl->GetBufferSize(code);
    void* out = malloc(*out_size);
    if (out != NULL) {
        memcpy(out, code->lpVtbl->GetBufferPointer(code), *out_size);
    }
    code->lpVtbl->Release(code);
    return out;
}
#endif

static SDL_GPUShader* load_shader(const char* shader_dir, const char* name, bool vertex, int num_samplers)
{
    SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(sfx_device);
    SDL_GPUShaderCreateInfo info;
    char path[512];
    size_t size = 0;
    void* code = NULL;

    SDL_zero(info);
    info.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = (Uint32)num_samplers;
    info.num_uniform_buffers = vertex ? 0 : 1;

    if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0) {
        snprintf(path, sizeof(path), "%s/%s.msl", shader_dir, name);
        code = read_file(path, &size);
        info.format = SDL_GPU_SHADERFORMAT_MSL;
        info.entrypoint = "main0";
#if defined(_WIN32)
    } else if ((formats & SDL_GPU_SHADERFORMAT_DXBC) != 0) {
        snprintf(path, sizeof(path), "%s/%s.hlsl", shader_dir, name);
        char* src = read_file(path, &size);
        if (src != NULL) {
            code = compile_hlsl(src, size, name, vertex, &size);
            free(src);
        }
        info.format = SDL_GPU_SHADERFORMAT_DXBC;
        info.entrypoint = "main";
#endif
    } else if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0) {
        snprintf(path, sizeof(path), "%s/%s.spv", shader_dir, name);
        code = read_file(path, &size);
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.entrypoint = "main";
    } else {
        SDL_Log("ScaleFX GPU: no usable shader format (0x%x)", (unsigned)formats);
        return NULL;
    }

    if (code == NULL) {
        SDL_Log("ScaleFX GPU: cannot load %s", path);
        return NULL;
    }

    info.code = (const Uint8*)code;
    info.code_size = size;
    SDL_GPUShader* shader = SDL_CreateGPUShader(sfx_device, &info);
    free(code);
    if (shader == NULL) {
        SDL_Log("ScaleFX GPU: %s: %s", name, SDL_GetError());
    }
    return shader;
}

static void free_targets(void)
{
    for (int i = 0; i < SFX_PASSES; i++) {
        if (sfx_passes[i].tex != NULL) {
            SDL_ReleaseGPUTexture(sfx_device, sfx_passes[i].tex);
            sfx_passes[i].tex = NULL;
        }
    }
    if (sfx_src_tex != NULL) {
        SDL_ReleaseGPUTexture(sfx_device, sfx_src_tex);
        sfx_src_tex = NULL;
    }
    if (sfx_upload != NULL) {
        SDL_ReleaseGPUTransferBuffer(sfx_device, sfx_upload);
        sfx_upload = NULL;
    }
    if (sfx_download != NULL) {
        SDL_ReleaseGPUTransferBuffer(sfx_device, sfx_download);
        sfx_download = NULL;
    }
    sfx_w = 0;
    sfx_h = 0;
}

static SDL_GPUTexture* make_texture(SDL_GPUTextureFormat format, int w, int h, bool target)
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
    return SDL_CreateGPUTexture(sfx_device, &info);
}

static bool make_targets(int w, int h)
{
    SDL_GPUTransferBufferCreateInfo tb;

    free_targets();

    sfx_src_tex = make_texture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, w, h, false);
    if (sfx_src_tex == NULL) {
        return false;
    }

    int cw = w;
    int ch = h;
    for (int i = 0; i < SFX_PASSES; i++) {
        SfxGpuPass* pass = &sfx_passes[i];
        pass->width = cw * sfx_scale[i];
        pass->height = ch * sfx_scale[i];
        pass->tex = make_texture(sfx_format[i], pass->width, pass->height, true);
        if (pass->tex == NULL) {
            SDL_Log("ScaleFX GPU: target %d: %s", i, SDL_GetError());
            return false;
        }
        cw = pass->width;
        ch = pass->height;
    }

    SDL_zero(tb);
    tb.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tb.size = (Uint32)w * (Uint32)h * 4;
    sfx_upload = SDL_CreateGPUTransferBuffer(sfx_device, &tb);

    // The readback buffer is only needed on the private-device path.
    if (sfx_own_device) {
        tb.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        tb.size = (Uint32)cw * (Uint32)ch * 4;
        sfx_download = SDL_CreateGPUTransferBuffer(sfx_device, &tb);
    }

    if (sfx_upload == NULL || (sfx_own_device && sfx_download == NULL)) {
        return false;
    }

    sfx_w = w;
    sfx_h = h;
    return true;
}

static bool sfx_setup(const char* shader_dir);

bool tig_scalefx_gpu_init(const char* shader_dir)
{
    if (sfx_ready) {
        return true;
    }
    if (sfx_failed) {
        return false;
    }
    sfx_failed = true;

    sfx_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL
#if defined(_WIN32)
            | SDL_GPU_SHADERFORMAT_DXBC
#endif
        ,
        false,
        NULL);
    if (sfx_device == NULL) {
        SDL_Log("ScaleFX GPU: no GPU device: %s", SDL_GetError());
        return false;
    }
    sfx_own_device = true;

    if (!sfx_setup(shader_dir)) {
        return false;
    }

    sfx_failed = false;
    sfx_ready = true;
    SDL_Log("ScaleFX GPU: ready (%s)", SDL_GetGPUDeviceDriver(sfx_device));
    return true;
}

bool tig_scalefx_gpu_attach(SDL_GPUDevice* device, const char* shader_dir)
{
    static bool attach_failed;

    if (sfx_ready && sfx_device == device) {
        return true;
    }
    if (attach_failed) {
        return false;
    }

    // Drop a private device from the readback path first.
    tig_scalefx_gpu_exit();
    sfx_failed = false;

    sfx_device = device;
    sfx_own_device = false;
    if (!sfx_setup(shader_dir)) {
        attach_failed = true;
        return false;
    }

    sfx_ready = true;
    SDL_Log("ScaleFX GPU: rendering on the renderer's device (%s)", SDL_GetGPUDeviceDriver(sfx_device));
    return true;
}

// Shaders, pipelines and the sampler on sfx_device.
static bool sfx_setup(const char* shader_dir)
{
    for (int i = 0; i < SFX_PASSES; i++) {
        if (!SDL_GPUTextureSupportsFormat(sfx_device, sfx_format[i], SDL_GPU_TEXTURETYPE_2D,
                SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)) {
            SDL_Log("ScaleFX GPU: texture format of pass %d unsupported", i);
            tig_scalefx_gpu_exit();
            return false;
        }
    }

    SDL_GPUShader* vs = load_shader(shader_dir, "scalefx-vert", true, 0);
    bool ok = vs != NULL;

    for (int i = 0; i < SFX_PASSES && ok; i++) {
        char name[32];
        snprintf(name, sizeof(name), "scalefx-pass%d", i);
        SDL_GPUShader* fs = load_shader(shader_dir, name, false, sfx_samplers[i]);
        if (fs == NULL) {
            ok = false;
            break;
        }

        SDL_GPUColorTargetDescription target;
        SDL_GPUGraphicsPipelineCreateInfo info;

        SDL_zero(target);
        target.format = sfx_format[i];

        SDL_zero(info);
        info.vertex_shader = vs;
        info.fragment_shader = fs;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.target_info.color_target_descriptions = &target;
        info.target_info.num_color_targets = 1;

        sfx_passes[i].pipeline = SDL_CreateGPUGraphicsPipeline(sfx_device, &info);
        SDL_ReleaseGPUShader(sfx_device, fs);
        if (sfx_passes[i].pipeline == NULL) {
            SDL_Log("ScaleFX GPU: pipeline %d: %s", i, SDL_GetError());
            ok = false;
        }
    }

    if (vs != NULL) {
        SDL_ReleaseGPUShader(sfx_device, vs);
    }

    if (ok) {
        SDL_GPUSamplerCreateInfo sampler;

        SDL_zero(sampler);
        sampler.min_filter = SDL_GPU_FILTER_NEAREST;
        sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
        sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sfx_sampler = SDL_CreateGPUSampler(sfx_device, &sampler);
        ok = sfx_sampler != NULL;
    }

    if (!ok) {
        tig_scalefx_gpu_exit();
        return false;
    }

    return true;
}

// Uploads the frame and records the five passes; the result ends up in the
// last pass's texture. Returns the command buffer (not submitted yet).
static SDL_GPUCommandBuffer* sfx_encode(const uint32_t* src, int src_pitch_px, int w, int h)
{
    if (!sfx_ready) {
        return NULL;
    }

    if ((w != sfx_w || h != sfx_h) && !make_targets(w, h)) {
        free_targets();
        return NULL;
    }

    // Upload the frame (XRGB8888 = BGRA bytes).
    uint8_t* up = (uint8_t*)SDL_MapGPUTransferBuffer(sfx_device, sfx_upload, true);
    if (up == NULL) {
        return NULL;
    }
    for (int y = 0; y < h; y++) {
        memcpy(up + (size_t)y * (size_t)w * 4, src + (size_t)y * (size_t)src_pitch_px, (size_t)w * 4);
    }
    SDL_UnmapGPUTransferBuffer(sfx_device, sfx_upload);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(sfx_device);
    if (cmd == NULL) {
        return NULL;
    }

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src_info;
    SDL_GPUTextureRegion region;
    SDL_zero(src_info);
    src_info.transfer_buffer = sfx_upload;
    src_info.pixels_per_row = (Uint32)w;
    src_info.rows_per_layer = (Uint32)h;
    SDL_zero(region);
    region.texture = sfx_src_tex;
    region.w = (Uint32)w;
    region.h = (Uint32)h;
    region.d = 1;
    SDL_UploadToGPUTexture(copy, &src_info, &region, false);
    SDL_EndGPUCopyPass(copy);

    for (int i = 0; i < SFX_PASSES; i++) {
        SfxGpuPass* pass = &sfx_passes[i];
        SDL_GPUTexture* in_tex = i == 0 ? sfx_src_tex : sfx_passes[i - 1].tex;
        int in_w = i == 0 ? w : sfx_passes[i - 1].width;
        int in_h = i == 0 ? h : sfx_passes[i - 1].height;

        SfxUniforms u;
        u.texture_size[0] = (float)in_w;
        u.texture_size[1] = (float)in_h;
        u.input_size[0] = (float)in_w;
        u.input_size[1] = (float)in_h;
        u.output_size[0] = (float)pass->width;
        u.output_size[1] = (float)pass->height;
        u.frame_count = 0;
        u.frame_direction = 1;
        SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof(u));

        SDL_GPUColorTargetInfo target;
        SDL_zero(target);
        target.texture = pass->tex;
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &target, 1, NULL);
        SDL_BindGPUGraphicsPipeline(rp, pass->pipeline);

        // Texture = previous pass; the second sampler is PassPrev2 (pass 0's
        // output) for pass 2 and PassPrev5 (the source frame) for pass 4.
        SDL_GPUTextureSamplerBinding bind[2];
        bind[0].texture = in_tex;
        bind[0].sampler = sfx_sampler;
        bind[1].texture = i == 2 ? sfx_passes[0].tex : sfx_src_tex;
        bind[1].sampler = sfx_sampler;
        SDL_BindGPUFragmentSamplers(rp, 0, bind, (Uint32)sfx_samplers[i]);

        SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
        SDL_EndGPURenderPass(rp);
    }

    return cmd;
}

SDL_GPUTexture* tig_scalefx_gpu_render(const uint32_t* src, int src_pitch_px, int w, int h)
{
    SDL_GPUCommandBuffer* cmd = sfx_encode(src, src_pitch_px, w, h);
    if (cmd == NULL) {
        return NULL;
    }

    // Same queue as the renderer: this runs before the frame that samples it.
    if (!SDL_SubmitGPUCommandBuffer(cmd)) {
        return NULL;
    }

    static int logged;
    if (!logged) {
        logged = 1;
        SDL_Log("ScaleFX GPU: direct frame %dx%d -> %dx%d", w, h, w * 3, h * 3);
    }
    return sfx_passes[SFX_PASSES - 1].tex;
}

bool tig_scalefx_gpu_run(const uint32_t* src, int src_pitch_px, int w, int h, uint32_t* dst, int dst_pitch_px)
{
    SDL_GPUCommandBuffer* cmd = sfx_encode(src, src_pitch_px, w, h);
    if (cmd == NULL) {
        return false;
    }

    SfxGpuPass* last = &sfx_passes[SFX_PASSES - 1];
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion region;
    SDL_zero(region);
    region.texture = last->tex;
    region.w = (Uint32)last->width;
    region.h = (Uint32)last->height;
    region.d = 1;
    SDL_GPUTextureTransferInfo dst_info;
    SDL_zero(dst_info);
    dst_info.transfer_buffer = sfx_download;
    dst_info.pixels_per_row = (Uint32)last->width;
    dst_info.rows_per_layer = (Uint32)last->height;
    SDL_DownloadFromGPUTexture(copy, &region, &dst_info);
    SDL_EndGPUCopyPass(copy);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == NULL) {
        return false;
    }
    SDL_WaitForGPUFences(sfx_device, true, &fence, 1);
    SDL_ReleaseGPUFence(sfx_device, fence);

    const uint8_t* down = (const uint8_t*)SDL_MapGPUTransferBuffer(sfx_device, sfx_download, false);
    if (down == NULL) {
        return false;
    }
    for (int y = 0; y < last->height; y++) {
        memcpy(dst + (size_t)y * (size_t)dst_pitch_px, down + (size_t)y * (size_t)last->width * 4, (size_t)last->width * 4);
    }
    SDL_UnmapGPUTransferBuffer(sfx_device, sfx_download);

    static int logged;
    if (!logged) {
        logged = 1;
        SDL_Log("ScaleFX GPU: first frame %dx%d -> %dx%d", w, h, last->width, last->height);
    }
    return true;
}

void tig_scalefx_gpu_exit(void)
{
    if (sfx_device == NULL) {
        return;
    }
    free_targets();
    for (int i = 0; i < SFX_PASSES; i++) {
        if (sfx_passes[i].pipeline != NULL) {
            SDL_ReleaseGPUGraphicsPipeline(sfx_device, sfx_passes[i].pipeline);
            sfx_passes[i].pipeline = NULL;
        }
    }
    if (sfx_sampler != NULL) {
        SDL_ReleaseGPUSampler(sfx_device, sfx_sampler);
        sfx_sampler = NULL;
    }
    if (sfx_own_device) {
        SDL_DestroyGPUDevice(sfx_device);
    }
    sfx_device = NULL;
    sfx_own_device = false;
    sfx_ready = false;
}
