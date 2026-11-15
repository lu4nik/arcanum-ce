// Local tweak: xBRZ (by Zenju, GPL-3, vendored in src/xbrz/) 3x upscale of the
// final frame, split into row slices processed in parallel. Experimental: too
// slow for a full 1152x720 frame every frame, kept to compare the look.

#include "tig/xbr.h"

#include <algorithm>
#include <vector>

#include "xbrz/xbrz.h"

#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#endif

extern "C" void tig_xbrz3x(const uint32_t* src, int src_pitch_px, int w, int h, uint32_t* dst, int dst_pitch_px)
{
    static std::vector<uint32_t> src_copy;
    static std::vector<uint32_t> dst_copy;

    src_copy.resize(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; y++) {
        const uint32_t* row = src + static_cast<size_t>(y) * src_pitch_px;
        std::transform(row, row + w, src_copy.data() + static_cast<size_t>(y) * w,
            [](uint32_t p) { return p & 0xFFFFFFu; });
    }
    const uint32_t* s = src_copy.data();

    uint32_t* t = dst;
    if (dst_pitch_px != w * 3) {
        dst_copy.resize(static_cast<size_t>(w) * 3 * h * 3);
        t = dst_copy.data();
    }

#if defined(__APPLE__)
    const int band = 16;
    const size_t bands = static_cast<size_t>((h + band - 1) / band);
    dispatch_apply(bands, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^(size_t i) {
        int y0 = static_cast<int>(i) * band;
        xbrz::scale(3, s, t, w, h, xbrz::ColorFormat::rgb, xbrz::ScalerCfg(), y0, y0 + band);
    });
#else
    xbrz::scale(3, s, t, w, h, xbrz::ColorFormat::rgb);
#endif

    if (t != dst) {
        for (int y = 0; y < h * 3; y++) {
            std::copy(t + static_cast<size_t>(y) * w * 3, t + static_cast<size_t>(y) * w * 3 + w * 3, dst + static_cast<size_t>(y) * dst_pitch_px);
        }
    }
}
