#ifndef ARCANUM_TIG_SCALEFX_GL_H_
#define ARCANUM_TIG_SCALEFX_GL_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Local tweak: ScaleFX 3x on the GPU (OpenGL on macOS, SDL GPU elsewhere). Pixels XRGB8888,
// pitches in pixels. shader_dir holds scalefx-pass0..4.glsl.
bool tig_scalefx_init(const char* shader_dir);
bool tig_scalefx_run(const uint32_t* src, int src_pitch_px, int w, int h, uint32_t* dst, int dst_pitch_px);
void tig_scalefx_exit(void);
// True when ScaleFX can run here (macOS OpenGL, or SDL GPU with shaders).
bool tig_scalefx_available(void);

// SDL GPU backend (scalefx_gpu.c): Direct3D 12 / Metal.
bool tig_scalefx_gpu_init(const char* shader_dir);
bool tig_scalefx_gpu_run(const uint32_t* src, int src_pitch_px, int w, int h, uint32_t* dst, int dst_pitch_px);
void tig_scalefx_gpu_exit(void);

// Direct path: run the passes on the SDL "gpu" renderer's own device and get
// the 3x result as a GPU texture (B8G8R8A8) to draw, without a readback.
// The texture stays owned by ScaleFX and changes when the frame size does.
struct SDL_GPUDevice;
struct SDL_GPUTexture;
bool tig_scalefx_gpu_attach(struct SDL_GPUDevice* device, const char* shader_dir);
struct SDL_GPUTexture* tig_scalefx_gpu_render(const uint32_t* src, int src_pitch_px, int w, int h);

#ifdef __cplusplus
}
#endif

#endif /* ARCANUM_TIG_SCALEFX_GL_H_ */
