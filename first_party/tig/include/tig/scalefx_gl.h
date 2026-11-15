#ifndef ARCANUM_TIG_SCALEFX_GL_H_
#define ARCANUM_TIG_SCALEFX_GL_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Local tweak: ScaleFX 3x on the GPU (OpenGL, macOS). Pixels XRGB8888,
// pitches in pixels. shader_dir holds scalefx-pass0..4.glsl.
bool tig_scalefx_init(const char* shader_dir);
bool tig_scalefx_run(const uint32_t* src, int src_pitch_px, int w, int h, uint32_t* dst, int dst_pitch_px);
void tig_scalefx_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* ARCANUM_TIG_SCALEFX_GL_H_ */
