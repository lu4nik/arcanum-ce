#ifndef TIG_LIT25D_H_
#define TIG_LIT25D_H_

// Local experiment: 2.5D per-pixel lighting on top of the software-rendered
// frame. The game keeps a "G-buffer" next to the iso window: every opaque
// object pixel stores the screen Y of its object's base (its foot on the
// ground), ground pixels store 0. From that the GPU pass rebuilds a rough 3D
// position for every pixel, lights it with point lights and casts shadows by
// marching through the same buffer.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TIG_LIT25D_MAX_LIGHTS 64

// G-buffer encoding (low 24 bits of each pixel).
#define TIG_LIT25D_CLASS_SHIFT 16
#define TIG_LIT25D_CLASS_OBJECT 1
#define TIG_LIT25D_CLASS_CRITTER 2
#define TIG_LIT25D_CLASS_WALL 3
#define TIG_LIT25D_CLASS_ROOF 4
// Added to a class: the pixel stands on an indoor tile (no sun there).
#define TIG_LIT25D_INDOOR 16

typedef struct TigLit25dLight {
    float x; // window coords of the light's ground point
    float y;
    float z; // height above the ground, pixels
    float radius;
    float r; // 0..1
    float g;
    float b;
} TigLit25dLight;

typedef struct TigLit25dFrame {
    const uint32_t* gbuf;
    int gbuf_pitch; // in pixels
    int x; // where the G-buffer (iso window) sits on screen
    int y;
    int width;
    int height;
    int origin_y; // decode: anchor = (stored + origin_y) as int16
    float ambient; // 0..1, brightness of the ambient tint already in the frame
    float sun; // 0..1 strength of sun shadows
    float sun_dir[3]; // towards the sun, world space (x right, y towards camera, z up)
    float sun_reach; // how far shadows are traced, world units
    int num_lights;
    TigLit25dLight lights[TIG_LIT25D_MAX_LIGHTS];
} TigLit25dFrame;

typedef bool (*TigLit25dProvider)(TigLit25dFrame* frame);

// 0 = off, 1 = on, 2 = G-buffer debug view.
int tig_lit25d_mode(void);
void tig_lit25d_set_mode(int mode);
void tig_lit25d_set_provider(TigLit25dProvider provider);

// Lights `src` into `dst` (same size, XRGB8888). Returns false when the pass is
// unavailable; `dst` is then untouched.
bool tig_lit25d_apply(const uint32_t* src, int src_pitch_px, int w, int h, uint32_t* dst, int dst_pitch_px);

#ifdef __cplusplus
}
#endif

#endif /* TIG_LIT25D_H_ */
