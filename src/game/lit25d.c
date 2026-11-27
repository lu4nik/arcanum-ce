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
            // The game draws a light's glow where the flame is, so that
            // point on screen is LIT25D_LAMP_H above the lamp's footprint.
            l->x = (float)(x + LIT25D_MARGIN);
            l->y = (float)(y + LIT25D_MARGIN) + LIT25D_LAMP_H;
            l->z = LIT25D_LAMP_H;
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
