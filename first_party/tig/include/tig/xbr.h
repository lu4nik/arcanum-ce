#ifndef ARCANUM_TIG_XBR_H_
#define ARCANUM_TIG_XBR_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Local tweak: xBR-style 3x upscale. Pixels are XRGB8888; pitches in pixels.
void tig_xbr3x(const uint32_t* src, int src_pitch_px, int w, int h, uint32_t* dst, int dst_pitch_px);

#ifdef __cplusplus
}
#endif

#endif /* ARCANUM_TIG_XBR_H_ */
