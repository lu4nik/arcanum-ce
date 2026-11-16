// Local tweak: xBR-style 3x edge-directed upscaler for the final frame.
//
// Written from the published description of Hyllian's xBR (level 2): for each
// source pixel and each of its four corners, compare the "edge" weight along
// the two diagonals in YUV space and, when one diagonal is clearly an edge,
// blend the corner cells of the 3x3 output block towards the neighbour colour.
// Shallow and steep lines additionally colour the adjacent edge cells.

#include "tig/xbr.h"

#include <stdlib.h>

#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#endif

// Per-pixel YUV, packed into 3 small ints.
typedef struct Yuv {
    short y;
    short u;
    short v;
} Yuv;

static Yuv* yuv_buf;
static int yuv_cap;

static inline int dist(const Yuv* a, const Yuv* b)
{
    return 48 * abs(a->y - b->y) + 7 * abs(a->u - b->u) + 6 * abs(a->v - b->v);
}

#define EQ_THRESHOLD 155

static inline uint32_t blend(uint32_t dst, uint32_t src, int a /* 0..256, weight of src */)
{
    uint32_t rb = (((src & 0xFF00FF) * a + (dst & 0xFF00FF) * (256 - a)) >> 8) & 0xFF00FF;
    uint32_t g = (((src & 0x00FF00) * a + (dst & 0x00FF00) * (256 - a)) >> 8) & 0x00FF00;
    return rb | g | 0xFF000000u;
}

int tig_xbr_is_checker(const uint32_t* src, int src_pitch, int w, int h, int x, int y)
{
    if (x <= 0 || y <= 0 || x >= w - 1 || y >= h - 1) {
        return 0;
    }
    const uint32_t* p = src + y * src_pitch + x;
    uint32_t e_px = p[0] | 0xFF000000u;
    uint32_t l = p[-1] | 0xFF000000u, rr = p[1] | 0xFF000000u;
    uint32_t u = p[-src_pitch] | 0xFF000000u, dn = p[src_pitch] | 0xFF000000u;
    uint32_t ul = p[-src_pitch - 1] | 0xFF000000u, ur = p[-src_pitch + 1] | 0xFF000000u;
    uint32_t dl = p[src_pitch - 1] | 0xFF000000u, dr = p[src_pitch + 1] | 0xFF000000u;
    int cross_same = (l == u) + (l == rr) + (l == dn);
    int diag_same = (ul == e_px) + (ur == e_px) + (dl == e_px) + (dr == e_px);
    return l != e_px && cross_same >= 2 && diag_same >= 3;
}

typedef struct Ctx {
    const uint32_t* src;
    int src_pitch;
    int w;
    int h;
    uint32_t* dst;
    int dst_pitch;
} Ctx;

// Rotations of a (dx, dy) offset: 0, 90, 180, 270 degrees.
static const int rot[4][2][2] = {
    { { 1, 0 }, { 0, 1 } },
    { { 0, -1 }, { 1, 0 } },
    { { -1, 0 }, { 0, -1 } },
    { { 0, 1 }, { -1, 0 } },
};

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void process_row(const Ctx* c, int y)
{
    for (int x = 0; x < c->w; x++) {
        uint32_t e_px = c->src[y * c->src_pitch + x] | 0xFF000000u;
        uint32_t out[9];
        for (int i = 0; i < 9; i++) {
            out[i] = e_px;
        }

        // Leave 1-pixel checkerboard dithering (the game's see-through walls
        // and shadows) as crisp blocks: edge rules turn it into a maze pattern.
        int checker = tig_xbr_is_checker(c->src, c->src_pitch, c->w, c->h, x, y);

        for (int r = 0; r < 4 && !checker; r++) {
            const int (*m)[2] = rot[r];

#define OFF(dx, dy) (clampi(y + m[1][0] * (dx) + m[1][1] * (dy), 0, c->h - 1) * c->w \
    + clampi(x + m[0][0] * (dx) + m[0][1] * (dy), 0, c->w - 1))
#define PX(i) (c->src[((i) / c->w) * c->src_pitch + ((i) % c->w)] | 0xFF000000u)
#define CELL(dx, dy) ((1 + m[1][0] * (dx) + m[1][1] * (dy)) * 3 + (1 + m[0][0] * (dx) + m[0][1] * (dy)))

            int iE = y * c->w + x;
            int iF = OFF(1, 0), iH = OFF(0, 1), iI = OFF(1, 1);
            int iB = OFF(0, -1), iD = OFF(-1, 0), iC = OFF(1, -1), iG = OFF(-1, 1);
            int iF4 = OFF(2, 0), iI4 = OFF(2, 1), iH5 = OFF(0, 2), iI5 = OFF(1, 2);

            const Yuv *E = &yuv_buf[iE], *F = &yuv_buf[iF], *H = &yuv_buf[iH], *I = &yuv_buf[iI];
            const Yuv *B = &yuv_buf[iB], *D = &yuv_buf[iD], *C = &yuv_buf[iC], *G = &yuv_buf[iG];
            const Yuv *F4 = &yuv_buf[iF4], *I4 = &yuv_buf[iI4], *H5 = &yuv_buf[iH5], *I5 = &yuv_buf[iI5];

            uint32_t pF = PX(iF), pH = PX(iH);
            if (e_px == pF || e_px == pH) {
                continue;
            }

            int e = dist(E, C) + dist(E, G) + dist(I, H5) + dist(I, F4) + 4 * dist(H, F);
            int i = dist(H, D) + dist(H, I5) + dist(F, I4) + dist(F, B) + 4 * dist(E, I);

#define EQ(a, b) (dist((a), (b)) < EQ_THRESHOLD)
            int n8 = CELL(1, 1), n5 = CELL(1, 0), n7 = CELL(0, 1), n2 = CELL(1, -1), n6 = CELL(-1, 1);

            if (e < i
                && ((!EQ(F, B) && !EQ(F, C)) || (!EQ(H, D) && !EQ(H, G))
                    || (EQ(E, I) && ((!EQ(F, F4) && !EQ(F, I4)) || (!EQ(H, H5) && !EQ(H, I5))))
                    || EQ(E, G) || EQ(E, C))) {
                int ke = dist(F, G);
                int ki = dist(H, C);
                int ex2 = PX(iC) != e_px && PX(iB) != PX(iC);
                int ex3 = PX(iG) != e_px && PX(iD) != PX(iG);
                uint32_t px = dist(E, F) <= dist(E, H) ? pF : pH;

                int shallow = 2 * ke <= ki && ex3;
                int steep = ke >= 2 * ki && ex2;

                if (shallow && steep) {
                    out[n7] = blend(out[n7], px, 192);
                    out[n5] = blend(out[n5], px, 192);
                    out[n6] = blend(out[n6], px, 64);
                    out[n2] = blend(out[n2], px, 64);
                    out[n8] = px;
                } else if (shallow) {
                    out[n7] = blend(out[n7], px, 192);
                    out[n6] = blend(out[n6], px, 64);
                    out[n5] = blend(out[n5], px, 64);
                    out[n8] = px;
                } else if (steep) {
                    out[n5] = blend(out[n5], px, 192);
                    out[n2] = blend(out[n2], px, 64);
                    out[n7] = blend(out[n7], px, 64);
                    out[n8] = px;
                } else {
                    out[n8] = blend(out[n8], px, 224);
                    out[n5] = blend(out[n5], px, 32);
                    out[n7] = blend(out[n7], px, 32);
                }
            } else if (e <= i) {
                uint32_t px = dist(E, F) <= dist(E, H) ? pF : pH;
                out[n8] = blend(out[n8], px, 128);
            }
#undef EQ
#undef OFF
#undef PX
#undef CELL
        }

        uint32_t* d = c->dst + (size_t)(y * 3) * c->dst_pitch + x * 3;
        for (int row = 0; row < 3; row++) {
            d[0] = out[row * 3 + 0];
            d[1] = out[row * 3 + 1];
            d[2] = out[row * 3 + 2];
            d += c->dst_pitch;
        }
    }
}

static void build_yuv(const Ctx* c, int y)
{
    for (int x = 0; x < c->w; x++) {
        uint32_t p = c->src[y * c->src_pitch + x];
        int r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
        Yuv* o = &yuv_buf[y * c->w + x];
        o->y = (short)((299 * r + 587 * g + 114 * b) / 1000);
        o->u = (short)((-169 * r - 331 * g + 500 * b) / 1000 + 128);
        o->v = (short)((500 * r - 419 * g - 81 * b) / 1000 + 128);
    }
}

void tig_xbr3x(const uint32_t* src, int src_pitch_px, int w, int h, uint32_t* dst, int dst_pitch_px)
{
    if (yuv_cap < w * h) {
        free(yuv_buf);
        yuv_buf = (Yuv*)malloc(sizeof(Yuv) * (size_t)w * (size_t)h);
        yuv_cap = w * h;
    }

    Ctx ctx = { src, src_pitch_px, w, h, dst, dst_pitch_px };
    const Ctx* c = &ctx;

#if defined(__APPLE__)
    const int band = 16;
    size_t bands = (size_t)((h + band - 1) / band);
    dispatch_queue_t q = dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0);
    dispatch_apply(bands, q, ^(size_t bi) {
        int y1 = (int)bi * band + band;
        for (int y = (int)bi * band; y < y1 && y < h; y++) {
            build_yuv(c, y);
        }
    });
    dispatch_apply(bands, q, ^(size_t bi) {
        int y1 = (int)bi * band + band;
        for (int y = (int)bi * band; y < y1 && y < h; y++) {
            process_row(c, y);
        }
    });
#else
    for (int y = 0; y < h; y++) {
        build_yuv(c, y);
    }
    for (int y = 0; y < h; y++) {
        process_row(c, y);
    }
#endif
}

#ifndef TIG_HAVE_XBRZ
// Fork tweak: xBRZ not built (GPL-3, personal builds only): use xBR instead.
void tig_xbrz3x(const uint32_t* src, int src_pitch_px, int w, int h, uint32_t* dst, int dst_pitch_px)
{
    tig_xbr3x(src, src_pitch_px, w, h, dst, dst_pitch_px);
}
#endif
