#ifndef ARCANUM_TIG_XBR_H_
#define ARCANUM_TIG_XBR_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Local tweak: xBR-style 3x upscale. Pixels are XRGB8888; pitches in pixels.
// Local tweak: xBRZ 3x (vendored, GPL-3), same contract as tig_xbr3x.
void tig_xbrz3x(const uint32_t* src, int src_pitch_px, int w, int h, uint32_t* dst, int dst_pitch_px);
// Local tweak: 1 if (x, y) is inside 1-pixel checkerboard dithering.
int tig_xbr_is_checker(const uint32_t* src, int src_pitch, int w, int h, int x, int y);

void tig_xbr3x(const uint32_t* src, int src_pitch_px, int w, int h, uint32_t* dst, int dst_pitch_px);

#ifdef __cplusplus
}
#endif

#endif /* ARCANUM_TIG_XBR_H_ */
