#include "game/lit25d.h"

#include <tig/lit25d.h>

#include "game/gamelib.h"
#include "game/light.h"
#include "game/location.h"
#include "game/object.h"
#include "game/roof.h"
#include "game/sector.h"
#include "game/sector_block_list.h"
#include "game/timeevent.h"

#include <math.h>

static tig_window_handle_t lit25d_window = TIG_WINDOW_HANDLE_INVALID;
static TigVideoBuffer* lit25d_gbuf;
static TigRect lit25d_window_rect;
static int lit25d_origin_y;
static TigPalette lit25d_palette;

// The G-buffer reaches this far past every screen edge, so shadows can be
// cast by things that are just out of view.
#define LIT25D_MARGIN 256
#define LIT25D_LAMP_H 55.0f
static bool lit25d_margin_dirty;
static bool lit25d_in_margin_pass;

static bool lit25d_provide(TigLit25dFrame* frame);
static void lit25d_char_seen(const TigArtBlitInfo* blit_info);
static void lit25d_char_normals(const TigArtBlitInfo* blit_info, const TigRect* dst_rect, uint32_t code);

// Glowing overlays seen recently: each is drawn piecewise (one piece per
// dirty rect), so pieces of the same effect are merged into one box.
#define LIT25D_MAX_EMITTERS 24
#define LIT25D_EMITTER_TTL 220
typedef struct LitEmitter {
    tig_art_id_t art_id;
    TigRect box; // window coords
    Uint64 seen;
} LitEmitter;
static LitEmitter lit25d_emitters[LIT25D_MAX_EMITTERS];
static int lit25d_tile_x;
static int lit25d_tile_y;
static void lit25d_add_emitters(TigLit25dFrame* frame, int num_static);

static void lit25d_destroy_buffer(void)
{
    if (lit25d_gbuf != NULL) {
        tig_video_buffer_destroy(lit25d_gbuf);
        lit25d_gbuf = NULL;
    }
}

static bool lit25d_create_buffer(tig_window_handle_t iso_window_handle)
{
    TigWindowData window_data;
    TigVideoBufferCreateInfo vb_create_info;

    lit25d_destroy_buffer();

    lit25d_window = iso_window_handle;
    if (tig_window_data(iso_window_handle, &window_data) != TIG_OK) {
        return false;
    }
    lit25d_window_rect = window_data.rect;

    vb_create_info.flags = TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY;
    vb_create_info.width = window_data.rect.width + 2 * LIT25D_MARGIN;
    vb_create_info.height = window_data.rect.height + 2 * LIT25D_MARGIN;
    vb_create_info.background_color = 0;
    vb_create_info.color_key = 0;
    if (tig_video_buffer_create(&vb_create_info, &lit25d_gbuf) != TIG_OK) {
        lit25d_gbuf = NULL;
        return false;
    }
    tig_video_buffer_fill(lit25d_gbuf, NULL, 0);
    lit25d_origin_y = 0;
    lit25d_margin_dirty = true;
    return true;
}

bool lit25d_init(tig_window_handle_t iso_window_handle)
{
    const char* env;

    if (!lit25d_create_buffer(iso_window_handle)) {
        return true; // Not fatal: the experiment just stays off.
    }

    tig_lit25d_set_provider(lit25d_provide);

    env = SDL_getenv("ARCANUM_LIT25D");
    if (env != NULL) {
        tig_lit25d_set_mode(SDL_atoi(env) % 3);
    }

    return true;
}

void lit25d_exit(void)
{
    tig_lit25d_set_provider(NULL);
    tig_lit25d_exit();
    lit25d_destroy_buffer();
    lit25d_window = TIG_WINDOW_HANDLE_INVALID;
}

void lit25d_resize(tig_window_handle_t iso_window_handle)
{
    lit25d_create_buffer(iso_window_handle);
}

bool lit25d_active(void)
{
    return lit25d_gbuf != NULL && tig_lit25d_mode() != 0;
}

void lit25d_cycle_mode(void)
{
    static const char* names[] = { "off", "on", "G-buffer view" };
    // Shift+F9 toggles off/on; the G-buffer view stays reachable only through
    // ARCANUM_LIT25D=2.
    int mode = tig_lit25d_mode() != 0 ? 0 : 1;

    tig_lit25d_set_mode(mode);
    tig_debug_printf("Lit25d: %s\n", names[mode]);
    lit25d_margin_dirty = true;

    // The game's own coarse light grid is switched off while the pass runs,
    // so everything has to be redrawn (and the G-buffer refilled).
    TigRect rect = { 0, 0, lit25d_window_rect.width, lit25d_window_rect.height };
    gamelib_invalidate_rect(&rect);
}

void lit25d_clear(const TigRect* rect)
{
    if (lit25d_gbuf != NULL) {
        TigRect tmp = *rect;
        // A full-window redraw also means the off-screen margin is stale.
        if (tmp.x <= 0 && tmp.y <= 0
            && tmp.x + tmp.width >= lit25d_window_rect.width
            && tmp.y + tmp.height >= lit25d_window_rect.height) {
            lit25d_margin_dirty = true;
        }
        tmp.x += LIT25D_MARGIN;
        tmp.y += LIT25D_MARGIN;
        tig_video_buffer_fill(lit25d_gbuf, &tmp, 0);
    }
}

// Mirrors tig_window_scroll for the G-buffer.
void lit25d_scroll(int dx, int dy)
{
    TigRect src_rect;
    TigRect dst_rect;
    TigVideoBufferBlitInfo vb_blit_info;

    if (lit25d_gbuf == NULL) {
        return;
    }

    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = lit25d_window_rect.width + 2 * LIT25D_MARGIN;
    src_rect.height = lit25d_window_rect.height + 2 * LIT25D_MARGIN;
    dst_rect = src_rect;

    if (dx < 0) {
        dst_rect.width += dx;
        src_rect.x -= dx;
        src_rect.width += dx;
    } else {
        dst_rect.x += dx;
        dst_rect.width -= dx;
        src_rect.width -= dx;
    }

    if (dy < 0) {
        dst_rect.height += dy;
        src_rect.y -= dy;
        src_rect.height += dy;
    } else {
        dst_rect.y += dy;
        dst_rect.height -= dy;
        src_rect.height -= dy;
    }

    vb_blit_info.flags = 0;
    vb_blit_info.src_video_buffer = lit25d_gbuf;
    vb_blit_info.src_rect = &src_rect;
    vb_blit_info.dst_video_buffer = lit25d_gbuf;
    vb_blit_info.dst_rect = &dst_rect;
    tig_video_buffer_blit(&vb_blit_info);

    // Stored anchors are relative to this origin, so moved pixels stay right.
    lit25d_origin_y += dy;
    lit25d_margin_dirty = true;
}

// Draws the same sprite into the G-buffer as one flat code: every opaque
// pixel gets the object's anchor (its foot on screen) and class.
void lit25d_blit(const TigArtBlitInfo* blit_info, int anchor_y, int cls)
{
    TigArtBlitInfo info;
    TigRect src_rect;
    TigRect dst_rect;
    uint32_t code;

    if (!lit25d_active()) {
        return;
    }

    // Additive glows and subtractive blob shadows are not solid. Neither are
    // see-through sprites (walls in front of the player are drawn as a
    // checkerboard): writing them would light the two halves of the checker
    // differently and the wall would look speckled.
    if ((blit_info->flags & (TIG_ART_BLT_BLEND_ADD | TIG_ART_BLT_BLEND_SUB | TIG_ART_BLT_BLEND_ALPHA_ANY)) != 0) {
        return;
    }

    // Anchors are stored in G-buffer coordinates, like the pixels themselves
    // (the buffer starts LIT25D_MARGIN above and left of the window).
    code = ((uint32_t)(anchor_y + LIT25D_MARGIN - lit25d_origin_y) & 0xFFFFu) | ((uint32_t)(cls & 31) << TIG_LIT25D_CLASS_SHIFT);
    for (int i = 0; i < 256; i++) {
        lit25d_palette.colors[i] = code;
    }

    info = *blit_info;
    src_rect = *blit_info->src_rect;
    dst_rect = *blit_info->dst_rect;
    dst_rect.x += LIT25D_MARGIN;
    dst_rect.y += LIT25D_MARGIN;
    info.flags = (blit_info->flags & (TIG_ART_BLT_FLIP_X | TIG_ART_BLT_FLIP_Y)) | TIG_ART_BLT_PALETTE_OVERRIDE;
    info.palette = &lit25d_palette;
    info.src_rect = &src_rect;
    info.dst_rect = &dst_rect;
    info.dst_video_buffer = lit25d_gbuf;
    info.scratch_video_buffer = NULL;
    tig_art_blit(&info);

    if ((cls & 15) == TIG_LIT25D_CLASS_CRITTER) {
        lit25d_char_seen(blit_info);
        lit25d_char_normals(blit_info, &dst_rect, code);
    }
}

static float lit25d_brightness(tig_color_t color)
{
    int r = tig_color_get_red(color);
    int g = tig_color_get_green(color);
    int b = tig_color_get_blue(color);
    return (float)(r * 30 + g * 59 + b * 11) / (255.0f * 100.0f);
}

static bool lit25d_provide(TigLit25dFrame* frame)
{
    TigVideoBufferData vb_data;
    TigWindowData window_data;
    TigRect screen_rect;
    LocRect loc_rect;
    SectorListNode* sectors;
    SectorListNode* node;
    Sector* sector;

    if (lit25d_gbuf == NULL || lit25d_window == TIG_WINDOW_HANDLE_INVALID) {
        return false;
    }

    if (tig_window_data(lit25d_window, &window_data) != TIG_OK) {
        return false;
    }

    // Pixels are only exposed while locked; system-memory surfaces keep the
    // same pointer after the unlock, so the pass can read it this frame.
    if (tig_video_buffer_lock(lit25d_gbuf) != TIG_OK) {
        return false;
    }
    int rc = tig_video_buffer_data(lit25d_gbuf, &vb_data);
    tig_video_buffer_unlock(lit25d_gbuf);
    if (rc != TIG_OK || vb_data.pixels == NULL) {
        return false;
    }

    frame->gbuf = (const uint32_t*)vb_data.pixels;
    frame->gbuf_pitch = vb_data.pitch / 4;
    frame->x = window_data.rect.x - LIT25D_MARGIN;
    frame->y = window_data.rect.y - LIT25D_MARGIN;
    frame->width = vb_data.width;
    frame->height = vb_data.height;
    frame->origin_y = lit25d_origin_y;
    frame->tile_x = lit25d_tile_x + LIT25D_MARGIN;
    frame->tile_y = lit25d_tile_y + LIT25D_MARGIN;

    frame->ambient = lit25d_brightness(light_get_outdoor_color());
    // Sun shadows fade in from dusk to full day.
    frame->sun = (frame->ambient - 0.45f) / 0.4f;
    if (frame->sun < 0.0f) frame->sun = 0.0f;
    if (frame->sun > 1.0f) frame->sun = 1.0f;

    // The sun follows the game clock: it rises on the left, crosses behind
    // the scene (so shadows fall towards the camera, where they can be seen)
    // and sets on the right; low sun = long shadows.
    {
        float t = (float)datetime_current_hour() + (float)datetime_current_minute() / 60.0f;
        float phase = (t - 6.0f) / 14.0f; // 6:00 .. 20:00
        if (phase < 0.0f) phase = 0.0f;
        if (phase > 1.0f) phase = 1.0f;
        float az = (-1.0f + 2.0f * phase) * 1.75f; // about +-100 degrees
        float el = 0.18f + 0.85f * sinf(phase * 3.14159265f); // radians above horizon
        frame->sun_dir[0] = cosf(el) * sinf(az);
        frame->sun_dir[1] = -cosf(el) * cosf(az);
        frame->sun_dir[2] = sinf(el);
        // Long enough for a ~150 px tall building at this elevation.
        float reach = 150.0f / tanf(el);
        frame->sun_reach = reach < 160.0f ? 160.0f : (reach > 520.0f ? 520.0f : reach);
        // Fade out towards sunrise and sunset.
        float edge = phase < 0.08f ? phase / 0.08f : (phase > 0.92f ? (1.0f - phase) / 0.08f : 1.0f);
        frame->sun *= edge;

        // Colour of the hour, outdoors only: golden near sunrise and
        // sunset, cool moonlight at night, neutral at midday.
        float night = 1.0f - (frame->ambient - 0.35f) / 0.45f;
        night = night < 0.0f ? 0.0f : (night > 1.0f ? 1.0f : night);
        float golden = 1.0f - (phase < 0.5f ? phase : 1.0f - phase) / 0.2f;
        golden = golden < 0.0f ? 0.0f : (golden > 1.0f ? 1.0f : golden);
        golden *= 1.0f - night;
        frame->night = night;
        frame->grade[0] = (1.0f + 0.10f * golden) * (1.0f - 0.16f * night);
        frame->grade[1] = (1.0f - 0.01f * golden) * (1.0f - 0.08f * night);
        frame->grade[2] = (1.0f - 0.15f * golden) * (1.0f + 0.08f * night);
    }

    // Diagnostics every ~3 seconds with ARCANUM_LIT25D_LOG=1: clock, sun, how
    // much of the G-buffer is filled (on screen and in the off-screen margin).
    {
        static Uint64 last_log;
        static int log_enabled = -1;
        if (log_enabled < 0) {
            log_enabled = SDL_getenv("ARCANUM_LIT25D_LOG") != NULL;
        }
        Uint64 now = SDL_GetTicks();
        if (log_enabled && now - last_log > 3000) {
            int filled = 0;
            int filled_margin = 0;
            for (int y = 0; y < vb_data.height; y += 4) {
                const uint32_t* row = frame->gbuf + (size_t)y * frame->gbuf_pitch;
                for (int x = 0; x < vb_data.width; x += 4) {
                    if ((row[x] & 0xFFFFFFu) != 0) {
                        bool inside = x >= LIT25D_MARGIN && y >= LIT25D_MARGIN
                            && x < vb_data.width - LIT25D_MARGIN && y < vb_data.height - LIT25D_MARGIN;
                        if (inside) {
                            filled++;
                        } else {
                            filled_margin++;
                        }
                    }
                }
            }
            tig_debug_printf("Lit25d: %02d:%02d ambient %.2f sun %.2f dir %.2f %.2f %.2f reach %.0f gbuf %d/%d\n",
                datetime_current_hour(), datetime_current_minute(),
                frame->ambient, frame->sun,
                frame->sun_dir[0], frame->sun_dir[1], frame->sun_dir[2], frame->sun_reach,
                filled, filled_margin);
            last_log = now;
        }
    }

    frame->num_lights = 0;

    screen_rect.x = -LIT25D_MARGIN - 200;
    screen_rect.y = -LIT25D_MARGIN - 200;
    screen_rect.width = frame->width + 400;
    screen_rect.height = frame->height + 400;
    if (!location_screen_rect_to_loc_rect(&screen_rect, &loc_rect)) {
        return true;
    }

    sectors = sector_list_create(&loc_rect);
    for (node = sectors; node != NULL; node = node->next) {
        if (!sector_lock(node->sec, &sector)) {
            continue;
        }

        for (SectorBlockListNode* light_node = sector->lights.head; light_node != NULL; light_node = light_node->next) {
            Light* light = (Light*)light_node->data;
            TigArtFrameData art_frame_data;
            int64_t x;
            int64_t y;

            if ((light->flags & (LF_OFF | LF_DARK)) != 0) {
                continue;
            }
            if (frame->num_lights >= TIG_LIT25D_MAX_LIGHTS) {
                break;
            }
            if (tig_art_frame_data(light->art_id, &art_frame_data) != TIG_OK) {
                continue;
            }

            location_xy(light->loc, &x, &y);
            x += light->offset_x + 40;
            y += light->offset_y + 20;
            if (x < -LIT25D_MARGIN - 300 || y < -LIT25D_MARGIN - 300
                || x > frame->width + 300 || y > frame->height + 300) {
                continue;
            }

            TigLit25dLight* l = &(frame->lights[frame->num_lights++]);
            // The light's hot spot: at the flame for some lamps, at the
            // base for others (candlesticks). z < 0 asks the pass to find the
            // flame in the frame and place the light from that.
            l->x = (float)(x + LIT25D_MARGIN);
            l->y = (float)(y + LIT25D_MARGIN);
            l->z = -1.0f;
            // The light art is the game's own falloff blob; its width is the
            // reach on screen.
            l->radius = (float)art_frame_data.width * 0.6f;
            if (l->radius < 60.0f) l->radius = 60.0f;

            int r = light->r;
            int g = light->g;
            int b = light->b;
            if (r == 0 && g == 0 && b == 0) {
                r = 255;
                g = 210;
                b = 150;
            }
            l->r = (float)r / 255.0f;
            l->g = (float)g / 255.0f;
            l->b = (float)b / 255.0f;
            // Warm lights are flames (torches, candles, gas): they breathe.
            l->flicker = r > b + 40 ? 0.09f : 0.02f;

            // Near-white lamps read as LEDs; nudge them towards gaslight.
            {
                float mx = l->r > l->g ? (l->r > l->b ? l->r : l->b) : (l->g > l->b ? l->g : l->b);
                float mn = l->r < l->g ? (l->r < l->b ? l->r : l->b) : (l->g < l->b ? l->g : l->b);
                float sat = mx > 0.0f ? (mx - mn) / mx : 0.0f;
                float warm = sat < 0.25f ? (0.25f - sat) / 0.25f : 0.0f;
                l->g *= 1.0f - 0.12f * warm;
                l->b *= 1.0f - 0.38f * warm;
            }
        }

        sector_unlock(node->sec);
    }
    sector_list_destroy(sectors);

    lit25d_add_emitters(frame, frame->num_lights);

    return true;
}

bool lit25d_margin_pass(void)
{
    return lit25d_in_margin_pass;
}

// Draws the objects and roofs around the screen into the G-buffer only.
void lit25d_draw_margin(GameDrawInfo* draw_info)
{
    TigRect strips[4];
    TigRectListNode* head = NULL;
    TigRectListNode* tail = NULL;
    TigRectListNode** saved_rects;
    int w = lit25d_window_rect.width;
    int h = lit25d_window_rect.height;
    int m = LIT25D_MARGIN;

    if (!lit25d_active() || !lit25d_margin_dirty) {
        return;
    }
    lit25d_margin_dirty = false;

    strips[0] = (TigRect){ -m, -m, w + 2 * m, m };
    strips[1] = (TigRect){ -m, h, w + 2 * m, m };
    strips[2] = (TigRect){ -m, 0, m, h };
    strips[3] = (TigRect){ w, 0, m, h };

    for (int i = 0; i < 4; i++) {
        TigRect tmp = strips[i];
        tmp.x += m;
        tmp.y += m;
        tig_video_buffer_fill(lit25d_gbuf, &tmp, 0);

        TigRectListNode* node = tig_rect_node_create();
        node->rect = strips[i];
        node->next = NULL;
        if (tail != NULL) {
            tail->next = node;
        } else {
            head = node;
        }
        tail = node;
    }

    saved_rects = draw_info->rects;
    draw_info->rects = &head;
    lit25d_in_margin_pass = true;
    object_draw(draw_info);
    roof_draw(draw_info);
    lit25d_in_margin_pass = false;
    draw_info->rects = saved_rects;

    while (head != NULL) {
        TigRectListNode* next = head->next;
        tig_rect_node_destroy(head);
        head = next;
    }
}

void lit25d_emit(const TigArtBlitInfo* blit_info)
{
    Uint64 now;
    tig_art_id_t art_id;
    const TigRect* r;
    int slot = -1;
    int oldest = 0;

    if (!lit25d_active() || lit25d_in_margin_pass) {
        return;
    }

    now = SDL_GetTicks();
    art_id = tig_art_id_frame_set(blit_info->art_id, 0);
    r = blit_info->dst_rect;

    for (int i = 0; i < LIT25D_MAX_EMITTERS; i++) {
        LitEmitter* e = &lit25d_emitters[i];
        if (e->seen != 0 && e->art_id == art_id
            && r->x < e->box.x + e->box.width + 48 && e->box.x < r->x + r->width + 48
            && r->y < e->box.y + e->box.height + 48 && e->box.y < r->y + r->height + 48) {
            slot = i;
            break;
        }
        if (lit25d_emitters[i].seen < lit25d_emitters[oldest].seen) {
            oldest = i;
        }
    }

    if (slot < 0) {
        slot = oldest;
        lit25d_emitters[slot].art_id = art_id;
        lit25d_emitters[slot].box = *r;
    } else {
        LitEmitter* e = &lit25d_emitters[slot];
        // A new animation frame starts a new box; pieces of the same frame
        // grow it.
        if (now - e->seen > 40) {
            e->box = *r;
        } else {
            int x0 = r->x < e->box.x ? r->x : e->box.x;
            int y0 = r->y < e->box.y ? r->y : e->box.y;
            int x1 = r->x + r->width > e->box.x + e->box.width ? r->x + r->width : e->box.x + e->box.width;
            int y1 = r->y + r->height > e->box.y + e->box.height ? r->y + r->height : e->box.y + e->box.height;
            e->box.x = x0;
            e->box.y = y0;
            e->box.width = x1 - x0;
            e->box.height = y1 - y0;
        }
    }
    lit25d_emitters[slot].seen = now;
}

// Turns live emitters into lights (called from the provider).
static void lit25d_add_emitters(TigLit25dFrame* frame, int num_static)
{
    Uint64 now = SDL_GetTicks();

    for (int i = 0; i < LIT25D_MAX_EMITTERS && frame->num_lights < TIG_LIT25D_MAX_LIGHTS; i++) {
        LitEmitter* e = &lit25d_emitters[i];
        TigArtFrameData fd;
        unsigned int color;
        int r = 255;
        int g = 150;
        int b = 70;

        if (e->seen == 0 || now - e->seen > LIT25D_EMITTER_TTL || e->box.width < 4 || e->box.height < 4) {
            continue;
        }

        float cx = (float)(e->box.x + e->box.width / 2 + LIT25D_MARGIN);
        float h = (float)e->box.height;
        float z = h * 0.4f;
        z = z < 10.0f ? 10.0f : (z > 80.0f ? 80.0f : z);
        float gy = (float)(e->box.y + LIT25D_MARGIN) + h * 0.5f + z;

        // Glows already attached to a lamp are covered by its light.
        bool near_lamp = false;
        for (int k = 0; k < num_static; k++) {
            float dx = frame->lights[k].x - cx;
            float dy = frame->lights[k].y - gy;
            if (dx * dx + dy * dy < 50.0f * 50.0f) {
                near_lamp = true;
                break;
            }
        }
        if (near_lamp) {
            continue;
        }

        // Colour of the effect: its centre pixel, unless that is too dark.
        if (tig_art_frame_data(e->art_id, &fd) == TIG_OK
            && sub_502E50(e->art_id, fd.width / 2, fd.height / 2, &color) == TIG_OK) {
            int cr = tig_color_get_red(color);
            int cg = tig_color_get_green(color);
            int cb = tig_color_get_blue(color);
            int mx = cr > cg ? (cr > cb ? cr : cb) : (cg > cb ? cg : cb);
            if (mx > 60) {
                r = cr * 255 / mx;
                g = cg * 255 / mx;
                b = cb * 255 / mx;
            }
        }

        TigLit25dLight* l = &frame->lights[frame->num_lights++];
        float size = (float)(e->box.width > e->box.height ? e->box.width : e->box.height);
        l->x = cx;
        l->y = gy;
        l->z = z;
        l->radius = size * 1.3f;
        l->radius = l->radius < 70.0f ? 70.0f : (l->radius > 260.0f ? 260.0f : l->radius);
        l->r = (float)r / 255.0f * 0.9f;
        l->g = (float)g / 255.0f * 0.9f;
        l->b = (float)b / 255.0f * 0.9f;
        l->flicker = 0.12f;
    }
}

// Text bubbles, floating text and dialog choices are drawn into the iso
// window on top of the world. Pixels they change are marked in the G-buffer
// (class 15) so the lighting pass leaves them alone.
static uint32_t* lit25d_ui_snapshot;
static size_t lit25d_ui_snapshot_size;

static bool lit25d_iso_pixels(TigVideoBuffer** vb, TigVideoBufferData* data)
{
    if (tig_window_vbid_get(lit25d_window, vb) != TIG_OK) {
        return false;
    }
    if (tig_video_buffer_lock(*vb) != TIG_OK) {
        return false;
    }
    if (tig_video_buffer_data(*vb, data) != TIG_OK || data->pixels == NULL || data->bpp != 32) {
        tig_video_buffer_unlock(*vb);
        return false;
    }
    return true;
}

static bool lit25d_clip(const TigRect* r, int w, int h, int* x0, int* y0, int* x1, int* y1)
{
    *x0 = r->x < 0 ? 0 : r->x;
    *y0 = r->y < 0 ? 0 : r->y;
    *x1 = r->x + r->width > w ? w : r->x + r->width;
    *y1 = r->y + r->height > h ? h : r->y + r->height;
    return *x0 < *x1 && *y0 < *y1;
}

void lit25d_ui_begin(GameDrawInfo* draw_info)
{
    TigVideoBuffer* vb;
    TigVideoBufferData data;

    if (!lit25d_active() || !lit25d_iso_pixels(&vb, &data)) {
        return;
    }

    size_t need = (size_t)data.width * (size_t)data.height;
    if (lit25d_ui_snapshot_size < need) {
        SDL_free(lit25d_ui_snapshot);
        lit25d_ui_snapshot = (uint32_t*)SDL_malloc(need * sizeof(uint32_t));
        lit25d_ui_snapshot_size = lit25d_ui_snapshot != NULL ? need : 0;
    }

    if (lit25d_ui_snapshot != NULL) {
        int pitch = data.pitch / 4;
        for (TigRectListNode* node = *draw_info->rects; node != NULL; node = node->next) {
            int x0, y0, x1, y1;
            if (!lit25d_clip(&node->rect, data.width, data.height, &x0, &y0, &x1, &y1)) {
                continue;
            }
            for (int y = y0; y < y1; y++) {
                SDL_memcpy(lit25d_ui_snapshot + (size_t)y * data.width + x0,
                    (const uint32_t*)data.pixels + (size_t)y * pitch + x0,
                    (size_t)(x1 - x0) * 4);
            }
        }
    }
    tig_video_buffer_unlock(vb);
}

void lit25d_ui_end(GameDrawInfo* draw_info)
{
    TigVideoBuffer* vb;
    TigVideoBufferData data;
    TigVideoBufferData gdata;

    if (!lit25d_active() || lit25d_ui_snapshot == NULL || !lit25d_iso_pixels(&vb, &data)) {
        return;
    }
    if (tig_video_buffer_lock(lit25d_gbuf) != TIG_OK) {
        tig_video_buffer_unlock(vb);
        return;
    }
    if (tig_video_buffer_data(lit25d_gbuf, &gdata) == TIG_OK && gdata.pixels != NULL
        && (size_t)data.width * (size_t)data.height <= lit25d_ui_snapshot_size) {
        int pitch = data.pitch / 4;
        int gpitch = gdata.pitch / 4;
        uint32_t ui_code = (uint32_t)15 << TIG_LIT25D_CLASS_SHIFT;
        for (TigRectListNode* node = *draw_info->rects; node != NULL; node = node->next) {
            int x0, y0, x1, y1;
            if (!lit25d_clip(&node->rect, data.width, data.height, &x0, &y0, &x1, &y1)) {
                continue;
            }
            for (int y = y0; y < y1; y++) {
                const uint32_t* now = (const uint32_t*)data.pixels + (size_t)y * pitch;
                const uint32_t* was = lit25d_ui_snapshot + (size_t)y * data.width;
                uint32_t* g = (uint32_t*)gdata.pixels + (size_t)(y + LIT25D_MARGIN) * gpitch + LIT25D_MARGIN;
                for (int x = x0; x < x1; x++) {
                    if (now[x] != was[x]) {
                        g[x] = ui_code;
                    }
                }
            }
        }
    }
    tig_video_buffer_unlock(lit25d_gbuf);
    tig_video_buffer_unlock(vb);
}

// Tile dump for the HD-tile experiment: if the game folder has a directory
// called lit25d_tiles, every floor tile drawn is saved there once, in its
// original colours (no light tint), as <art id>.bmp. Magenta = transparent.
void lit25d_tile_seen(tig_art_id_t art_id)
{
    static int enabled = -1;
    static tig_art_id_t seen[8192];
    TigArtFrameData fd;
    TigVideoBufferCreateInfo vb_info;
    TigVideoBuffer* vb;

    if (enabled < 0) {
        SDL_PathInfo info;
        enabled = SDL_GetPathInfo("lit25d_tiles", &info) && info.type == SDL_PATHTYPE_DIRECTORY;
        if (enabled) {
            tig_debug_printf("Lit25d: dumping floor tiles to lit25d_tiles/\n");
        }
    }
    if (!enabled || art_id == 0 || art_id == TIG_ART_ID_INVALID) {
        return;
    }

    unsigned int h = (art_id * 2654435761u) & 8191u;
    for (int i = 0; i < 8192; i++) {
        unsigned int k = (h + (unsigned int)i) & 8191u;
        if (seen[k] == art_id) {
            return;
        }
        if (seen[k] == 0) {
            seen[k] = art_id;
            break;
        }
    }

    if (tig_art_frame_data(art_id, &fd) != TIG_OK || fd.width <= 0 || fd.height <= 0) {
        return;
    }

    vb_info.flags = TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY;
    vb_info.width = fd.width;
    vb_info.height = fd.height;
    vb_info.background_color = tig_color_make(255, 0, 255);
    vb_info.color_key = 0;
    if (tig_video_buffer_create(&vb_info, &vb) != TIG_OK) {
        return;
    }
    tig_video_buffer_fill(vb, NULL, tig_color_make(255, 0, 255));

    TigRect rect = { 0, 0, fd.width, fd.height };
    TigArtBlitInfo blit;
    SDL_zero(blit);
    blit.flags = TIG_ART_BLT_PALETTE_ORIGINAL;
    blit.art_id = art_id;
    blit.src_rect = &rect;
    blit.dst_rect = &rect;
    blit.dst_video_buffer = vb;
    if (tig_art_blit(&blit) == TIG_OK) {
        TigVideoBufferSaveToBmpInfo save;
        SDL_zero(save);
        save.video_buffer = vb;
        save.rect = &rect;
        SDL_snprintf(save.path, sizeof(save.path), "lit25d_tiles/%08X.bmp", (unsigned int)art_id);
        tig_video_buffer_save_to_bmp(&save);
    }
    tig_video_buffer_destroy(vb);
}

// Floor capture for the HD-tile experiment (Shift+F10): the next full redraw
// also draws every floor tile, untinted and unclipped, into a window-sized
// buffer, saved as lit25d_ground/ground_NNN.bmp with ground_NNN.txt listing
// "art_id x y" for each tile. The upscaler cuts HD tiles back out of it.
static bool lit25d_capture_pending;
static bool lit25d_capturing;
static TigVideoBuffer* lit25d_capture_vb;
static SDL_IOStream* lit25d_capture_list;
static char lit25d_capture_name[64];

void lit25d_capture_request(void)
{
    lit25d_capture_pending = true;
    TigRect rect = { 0, 0, lit25d_window_rect.width, lit25d_window_rect.height };
    gamelib_invalidate_rect(&rect);
}

void lit25d_capture_begin(void)
{
    TigVideoBufferCreateInfo vb_info;

    if (!lit25d_capture_pending) {
        return;
    }
    lit25d_capture_pending = false;
    SDL_CreateDirectory("lit25d_ground");

    for (int n = 0; n < 1000; n++) {
        char path[96];
        SDL_PathInfo info;
        SDL_snprintf(lit25d_capture_name, sizeof(lit25d_capture_name), "lit25d_ground/ground_%03d", n);
        SDL_snprintf(path, sizeof(path), "%s.bmp", lit25d_capture_name);
        if (!SDL_GetPathInfo(path, &info)) {
            break;
        }
    }

    vb_info.flags = TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY;
    vb_info.width = lit25d_window_rect.width;
    vb_info.height = lit25d_window_rect.height;
    vb_info.background_color = tig_color_make(255, 0, 255);
    vb_info.color_key = 0;
    if (tig_video_buffer_create(&vb_info, &lit25d_capture_vb) != TIG_OK) {
        return;
    }
    tig_video_buffer_fill(lit25d_capture_vb, NULL, tig_color_make(255, 0, 255));

    char list_path[96];
    SDL_snprintf(list_path, sizeof(list_path), "%s.txt", lit25d_capture_name);
    lit25d_capture_list = SDL_IOFromFile(list_path, "w");
    lit25d_capturing = true;
}

void lit25d_tile_placed(tig_art_id_t art_id, const TigRect* rect)
{
    // Any tile's position fixes the floor lattice for the HD pass.
    lit25d_tile_x = rect->x;
    lit25d_tile_y = rect->y;

    if (!lit25d_capturing || lit25d_capture_vb == NULL) {
        return;
    }

    TigRect dst = *rect;
    TigRect src = { 0, 0, rect->width, rect->height };
    TigArtBlitInfo blit;
    SDL_zero(blit);
    blit.flags = TIG_ART_BLT_PALETTE_ORIGINAL;
    blit.art_id = art_id;
    blit.src_rect = &src;
    blit.dst_rect = &dst;
    blit.dst_video_buffer = lit25d_capture_vb;
    tig_art_blit(&blit);

    if (lit25d_capture_list != NULL) {
        SDL_IOprintf(lit25d_capture_list, "%08X %d %d\n", (unsigned int)art_id, rect->x, rect->y);
    }
}

void lit25d_capture_end(void)
{
    if (!lit25d_capturing) {
        return;
    }
    lit25d_capturing = false;

    if (lit25d_capture_vb != NULL) {
        TigVideoBufferSaveToBmpInfo save;
        SDL_zero(save);
        save.video_buffer = lit25d_capture_vb;
        SDL_snprintf(save.path, sizeof(save.path), "%s.bmp", lit25d_capture_name);
        tig_video_buffer_save_to_bmp(&save);
        tig_video_buffer_destroy(lit25d_capture_vb);
        lit25d_capture_vb = NULL;
        tig_debug_printf("Lit25d: floor saved to %s.bmp\n", lit25d_capture_name);
    }
    if (lit25d_capture_list != NULL) {
        SDL_CloseIO(lit25d_capture_list);
        lit25d_capture_list = NULL;
    }
}

// HD floor tiles (lit25d_hd/atlas.bin, made by the upscaler): sorted art ids
// and their images. A tile's slot + 1 goes into its floor pixels' G-buffer
// code, the HD pass on the upscaled frame draws the HD image there.
static uint32_t* lit25d_hd_ids;
static int lit25d_hd_count;

static void lit25d_hd_load(void)
{
    static bool tried;
    SDL_IOStream* io;
    uint32_t head[4];

    if (tried) {
        return;
    }
    tried = true;

    io = SDL_IOFromFile("lit25d_hd/atlas.bin", "rb");
    if (io == NULL) {
        return;
    }
    if (SDL_ReadIO(io, head, sizeof(head)) == sizeof(head) && head[0] == 0x4135324Cu /* L25A */
        && head[3] > 0 && head[3] < 65535) {
        int w = (int)head[1];
        int h = (int)head[2];
        int n = (int)head[3];
        size_t pixels = (size_t)w * (size_t)h * 4 * (size_t)n;
        uint32_t* ids = (uint32_t*)SDL_malloc(sizeof(uint32_t) * (size_t)n);
        uint8_t* rgba = (uint8_t*)SDL_malloc(pixels);
        if (ids != NULL && rgba != NULL
            && SDL_ReadIO(io, ids, sizeof(uint32_t) * (size_t)n) == sizeof(uint32_t) * (size_t)n
            && SDL_ReadIO(io, rgba, pixels) == pixels) {
            tig_lit25d_set_hd_atlas(rgba, w, h, n);
            lit25d_hd_ids = ids;
            lit25d_hd_count = n;
            ids = NULL;
            tig_debug_printf("Lit25d: %d HD floor tiles\n", n);
        }
        SDL_free(ids);
        SDL_free(rgba);
    }
    SDL_CloseIO(io);
}

int lit25d_hd_slot(tig_art_id_t art_id)
{
    lit25d_hd_load();
    int lo = 0;
    int hi = lit25d_hd_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (lit25d_hd_ids[mid] == (uint32_t)art_id) {
            return mid;
        }
        if (lit25d_hd_ids[mid] < (uint32_t)art_id) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return -1;
}

// Floor tile into the G-buffer: class 0 (ground), the indoor flag and the HD
// slot (+1) in the low 16 bits.
void lit25d_blit_floor(const TigArtBlitInfo* blit_info, bool indoor, int slot)
{
    TigArtBlitInfo info;
    TigRect src_rect;
    TigRect dst_rect;
    uint32_t code;

    if (!lit25d_active()) {
        return;
    }

    code = (uint32_t)(slot + 1) & 0xFFFFu;
    if (indoor) {
        code |= (uint32_t)TIG_LIT25D_INDOOR << TIG_LIT25D_CLASS_SHIFT;
    }
    if (code == 0) {
        return;
    }
    for (int i = 0; i < 256; i++) {
        lit25d_palette.colors[i] = code;
    }

    info = *blit_info;
    src_rect = *blit_info->src_rect;
    dst_rect = *blit_info->dst_rect;
    dst_rect.x += LIT25D_MARGIN;
    dst_rect.y += LIT25D_MARGIN;
    info.flags = (blit_info->flags & (TIG_ART_BLT_FLIP_X | TIG_ART_BLT_FLIP_Y)) | TIG_ART_BLT_PALETTE_OVERRIDE;
    info.palette = &lit25d_palette;
    info.src_rect = &src_rect;
    info.dst_rect = &dst_rect;
    info.dst_video_buffer = lit25d_gbuf;
    info.scratch_video_buffer = NULL;
    tig_art_blit(&info);
}

// Character normals (offline Marigold, see ~/Games/lit25d-ai). Frames are
// keyed by art id without palette plus the horizontal flip, and stored as
// they appear on screen, so a blit's src_rect indexes them directly.
static uint32_t lit25d_char_key_art(const TigArtBlitInfo* blit_info)
{
    return (uint32_t)tig_art_id_palette_set(blit_info->art_id, 0);
}

static int lit25d_char_key_flip(const TigArtBlitInfo* blit_info)
{
    return (blit_info->flags & TIG_ART_BLT_FLIP_X) != 0 ? 1 : 0;
}

// With a lit25d_chars folder next to the game, every character frame drawn
// is saved once as <art>_<flip>.bmp (magenta = transparent).
static void lit25d_char_seen(const TigArtBlitInfo* blit_info)
{
    static int enabled = -1;
    static uint32_t seen[16384];
    TigArtFrameData fd;
    TigVideoBufferCreateInfo vb_info;
    TigVideoBuffer* vb;

    if (enabled < 0) {
        SDL_PathInfo pi;
        enabled = SDL_GetPathInfo("lit25d_chars", &pi) && pi.type == SDL_PATHTYPE_DIRECTORY;
        if (enabled) {
            tig_debug_printf("Lit25d: dumping character frames to lit25d_chars/\n");
        }
    }
    if (!enabled || lit25d_in_margin_pass) {
        return;
    }

    tig_art_id_t art_id = (tig_art_id_t)lit25d_char_key_art(blit_info);
    int flip = lit25d_char_key_flip(blit_info);
    // Low bit of the stored value carries the flip; art ids keep it clear
    // in the hash only, the file name has both.
    uint32_t key = (uint32_t)art_id * 2u + (uint32_t)flip + 1u;
    unsigned int h = (key * 2654435761u) & 16383u;
    for (int i = 0; i < 16384; i++) {
        unsigned int k = (h + (unsigned int)i) & 16383u;
        if (seen[k] == key) {
            return;
        }
        if (seen[k] == 0) {
            seen[k] = key;
            break;
        }
    }

    if (tig_art_frame_data(blit_info->art_id, &fd) != TIG_OK || fd.width <= 0 || fd.height <= 0) {
        return;
    }

    vb_info.flags = TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY;
    vb_info.width = fd.width;
    vb_info.height = fd.height;
    vb_info.background_color = tig_color_make(255, 0, 255);
    vb_info.color_key = 0;
    if (tig_video_buffer_create(&vb_info, &vb) != TIG_OK) {
        return;
    }
    tig_video_buffer_fill(vb, NULL, tig_color_make(255, 0, 255));

    TigRect rect = { 0, 0, fd.width, fd.height };
    TigArtBlitInfo blit;
    SDL_zero(blit);
    blit.flags = TIG_ART_BLT_PALETTE_ORIGINAL | (flip ? TIG_ART_BLT_FLIP_X : 0);
    blit.art_id = blit_info->art_id;
    blit.src_rect = &rect;
    blit.dst_rect = &rect;
    blit.dst_video_buffer = vb;
    if (tig_art_blit(&blit) == TIG_OK) {
        TigVideoBufferSaveToBmpInfo save;
        SDL_zero(save);
        save.video_buffer = vb;
        save.rect = &rect;
        SDL_snprintf(save.path, sizeof(save.path), "lit25d_chars/%08X_%d.bmp", (unsigned int)art_id, flip);
        tig_video_buffer_save_to_bmp(&save);
    }
    tig_video_buffer_destroy(vb);
}

// lit25d_hd/chars.bin: "L25N", count, then count entries {art, flip, w, h,
// offset} sorted by (art, flip), then one byte per pixel: high nibble the
// screen-space normal's x, low nibble its y (up), each 1..14; 0 = none.
typedef struct LitCharNormals {
    uint32_t art;
    uint32_t flip;
    uint32_t width;
    uint32_t height;
    uint32_t offset;
} LitCharNormals;
static LitCharNormals* lit25d_cn_entries;
static uint8_t* lit25d_cn_data;
static uint32_t lit25d_cn_count;
static bool lit25d_cn_loaded;

static void lit25d_cn_load(void)
{
    size_t size;
    uint8_t* buf;

    lit25d_cn_loaded = true;
    buf = (uint8_t*)SDL_LoadFile("lit25d_hd/chars.bin", &size);
    if (buf == NULL) {
        return;
    }
    if (size < 8 || SDL_memcmp(buf, "L25N", 4) != 0) {
        SDL_free(buf);
        return;
    }
    uint32_t n;
    SDL_memcpy(&n, buf + 4, 4);
    if (size < 8 + (size_t)n * sizeof(LitCharNormals)) {
        SDL_free(buf);
        return;
    }
    lit25d_cn_entries = (LitCharNormals*)(buf + 8);
    lit25d_cn_data = buf + 8 + (size_t)n * sizeof(LitCharNormals);
    lit25d_cn_count = n;
    tig_debug_printf("Lit25d: %u character frames with normals\n", (unsigned int)n);
}

static const LitCharNormals* lit25d_cn_find(uint32_t art, uint32_t flip)
{
    uint32_t lo = 0;
    uint32_t hi = lit25d_cn_count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        const LitCharNormals* e = &lit25d_cn_entries[mid];
        if (e->art < art || (e->art == art && e->flip < flip)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < lit25d_cn_count && lit25d_cn_entries[lo].art == art && lit25d_cn_entries[lo].flip == flip) {
        return &lit25d_cn_entries[lo];
    }
    return NULL;
}

// Puts the frame's normals into the top byte of the G-buffer pixels this
// blit just wrote (those still holding its code).
static void lit25d_char_normals(const TigArtBlitInfo* blit_info, const TigRect* dst_rect, uint32_t code)
{
    TigVideoBufferData vb_data;

    if (!lit25d_cn_loaded) {
        lit25d_cn_load();
    }
    if (lit25d_cn_count == 0) {
        return;
    }
    const LitCharNormals* e = lit25d_cn_find(lit25d_char_key_art(blit_info), (uint32_t)lit25d_char_key_flip(blit_info));
    if (e == NULL) {
        return;
    }
    if (tig_video_buffer_lock(lit25d_gbuf) != TIG_OK) {
        return;
    }
    if (tig_video_buffer_data(lit25d_gbuf, &vb_data) == TIG_OK && vb_data.pixels != NULL) {
        const uint8_t* nm = lit25d_cn_data + e->offset;
        const TigRect* src = blit_info->src_rect;
        int w = SDL_min(dst_rect->width, src->width);
        int h = SDL_min(dst_rect->height, src->height);
        for (int y = 0; y < h; y++) {
            int gy = dst_rect->y + y;
            int sy = src->y + y;
            if (gy < 0 || gy >= vb_data.height || sy < 0 || sy >= (int)e->height) {
                continue;
            }
            uint32_t* row = (uint32_t*)((uint8_t*)vb_data.pixels + (size_t)gy * (size_t)vb_data.pitch);
            for (int x = 0; x < w; x++) {
                int gx = dst_rect->x + x;
                int sx = src->x + x;
                if (gx < 0 || gx >= vb_data.width || sx < 0 || sx >= (int)e->width) {
                    continue;
                }
                if ((row[gx] & 0xFFFFFFu) != (code & 0xFFFFFFu)) {
                    continue;
                }
                uint8_t v = nm[(size_t)sy * e->width + (size_t)sx];
                if (v != 0) {
                    row[gx] = (row[gx] & 0xFFFFFFu) | ((uint32_t)v << 24);
                }
            }
        }
    }
    tig_video_buffer_unlock(lit25d_gbuf);
}
