#include "tig/video.h"

#include <limits.h>
#include <stdio.h>

#include "tig/art.h"
#include "tig/color.h"
#include "tig/core.h"
#include "tig/debug.h"
#include "tig/draw.h"
#include "tig/file.h"
#include "tig/memory.h"
#include "tig/window.h"
#include "tig/scalefx_gl.h"
#include "tig/xbr.h"
#include "tig/message.h"
#include "tig/mouse.h"
#include "tig/timer.h"
#include "tig/window.h"

typedef struct TigVideoBuffer {
    TigVideoBufferFlags flags;
    TigRect frame;
    int texture_width;
    int texture_height;
    unsigned int background_color;
    unsigned int color_key;
    SDL_Surface* surface;
    int lock_count;
} TigVideoBuffer;

typedef struct TigVideoState {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    SDL_Surface* surface;
    int fps;
} TigVideoState;

typedef struct TigFadeState {
    bool enabled;
    SDL_Color color;
} TigFadeState;

static bool tig_video_window_create(TigInitInfo* init_info);
static void tig_video_window_destroy(void);
static bool sub_524830(void);
static int tig_video_screenshot_make_internal(int key);
static int tig_video_buffer_data_to_bmp(SDL_Surface* surface, TigRect* rect, const char* file_name);

// 0x5BF3D8
static int tig_video_screenshot_key = -1;

// 0x60F250
static TigVideoState tig_video_state;

// 0x60FEF8
static float tig_video_gamma;

// 0x61030C
static int tig_video_bpp;

// 0x61031C
static bool tig_video_3d_initialized;

// 0x610320
static bool tig_video_3d_is_hardware;

// 0x610358
static bool tig_video_3d_scene_started;

// 0x610388
static TigRect stru_610388;

// 0x61039C
static bool tig_video_initialized;

// 0x6103A0
static bool tig_video_show_fps;

// 0x6103A4
static int dword_6103A4;

static TigFadeState tig_fade_state;

// 0x51F330
int tig_video_init(TigInitInfo* init_info)
{
    memset(&tig_video_state, 0, sizeof(tig_video_state));

    // Local tweak: 0x0 asks for an automatic size (tig_video_window_create).
    if ((init_info->width != 0 || init_info->height != 0)
        && (init_info->width < 800 || init_info->height < 600)) {
        return TIG_ERR_GENERIC;
    }

    if (init_info->bpp != 32) {
        return TIG_ERR_GENERIC;
    }

    if (!tig_video_window_create(init_info)) {
        return TIG_ERR_GENERIC;
    }

    if (!sub_524830()) {
        tig_video_window_destroy();
        return TIG_ERR_GENERIC;
    }

    tig_video_show_fps = (init_info->flags & TIG_INITIALIZE_FPS) != 0;
    tig_video_bpp = init_info->bpp;

    SDL_HideCursor();

    tig_video_screenshot_key = -1;
    dword_6103A4 = 0;

    tig_video_initialized = true;

    return TIG_OK;
}

// 0x51F3F0
void tig_video_exit(void)
{
    tig_video_window_destroy();
    tig_video_initialized = false;
}

int tig_video_window_get(SDL_Window** window_ptr)
{
    if (!tig_video_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    *window_ptr = tig_video_state.window;

    return TIG_OK;
}

int tig_video_renderer_get(SDL_Renderer** renderer_ptr)
{
    if (!tig_video_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    *renderer_ptr = tig_video_state.renderer;

    return TIG_OK;
}

// 0x51F4A0
void tig_video_display_fps(void)
{
    // 0x60F248
    static tig_timestamp_t curr;

    // 0x610380
    static tig_timestamp_t prev;

    // 0x610384
    static unsigned int counter;

    tig_duration_t elapsed;

    if (tig_video_show_fps) {
        ++counter;
        if (counter >= 10) {
            tig_timer_now(&curr);
            elapsed = tig_timer_between(prev, curr);
            tig_video_state.fps = (int)((float)counter / ((float)elapsed / 1000.0f));
            prev = curr;
            counter = 0;
        }
    }
}

// 0x51F600
int tig_video_blit(TigVideoBuffer* src_video_buffer, TigRect* src_rect, TigRect* dst_rect)
{
    int rc;
    TigRect clamped_dst_rect;
    SDL_Rect native_src_rect;
    SDL_Rect native_dst_rect;

    if (!tig_video_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    rc = tig_rect_intersection(dst_rect, &stru_610388, &clamped_dst_rect);
    if (rc != TIG_OK) {
        return rc;
    }

    native_src_rect.x = src_rect->x;
    native_src_rect.y = src_rect->y;
    native_src_rect.w = src_rect->width;
    native_src_rect.h = src_rect->height;

    native_dst_rect.x = clamped_dst_rect.x;
    native_dst_rect.y = clamped_dst_rect.y;
    native_dst_rect.w = clamped_dst_rect.width;
    native_dst_rect.h = clamped_dst_rect.height;

    SDL_BlitSurface(src_video_buffer->surface,
        &native_src_rect,
        tig_video_state.surface,
        &native_dst_rect);

    return TIG_OK;
}

// 0x51F7C0
int tig_video_fill(const TigRect* rect, tig_color_t color)
{
    int rc;
    TigRect clamped_rect;
    SDL_Rect native_rect;

    if (!tig_video_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    rc = tig_rect_intersection(rect != NULL ? rect : &stru_610388,
        &stru_610388,
        &clamped_rect);
    if (rc != TIG_OK) {
        return rc;
    }

    native_rect.x = clamped_rect.x;
    native_rect.y = clamped_rect.y;
    native_rect.w = clamped_rect.width;
    native_rect.h = clamped_rect.height;

    if (!SDL_FillSurfaceRect(tig_video_state.surface, &native_rect, color)) {
        return TIG_ERR_GENERIC;
    }

    return TIG_OK;
}

// 0x51F8F0
// Local tweak: optional xBR 3x upscale of the final frame.
static int tig_video_smooth;
static int tig_video_smooth_label_frames;

// Local tweak: gamepad button chart drawn over the game while visible.
static bool tig_video_overlay_visible;
static SDL_Texture* tig_video_overlay_texture;
static bool tig_video_overlay_failed;

void tig_video_set_overlay_visible(bool visible)
{
    tig_video_overlay_visible = visible;
}

static void tig_video_overlay_draw(void)
{
    SDL_Renderer* renderer = tig_video_state.renderer;
    int lw;
    int lh;
    int ow;
    int oh;
    float scale;
    SDL_FRect dst;
    SDL_RendererLogicalPresentation mode;

    if (!tig_video_overlay_visible || tig_video_overlay_failed) {
        return;
    }

    if (tig_video_overlay_texture == NULL) {
        SDL_Surface* surface = SDL_LoadBMP("HighRes/gamepad.bmp");
        if (surface == NULL) {
            tig_debug_printf("TIG Video: no gamepad chart: %s\n", SDL_GetError());
            tig_video_overlay_failed = true;
            return;
        }
        tig_video_overlay_texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);
        if (tig_video_overlay_texture == NULL) {
            tig_video_overlay_failed = true;
            return;
        }
        SDL_SetTextureScaleMode(tig_video_overlay_texture, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureAlphaMod(tig_video_overlay_texture, 235);
    }

    // Draw in output pixels so the chart stays sharp whatever the game's
    // logical resolution is.
    SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
    SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    if (SDL_GetCurrentRenderOutputSize(renderer, &ow, &oh)) {
        scale = SDL_min((float)ow * 0.9f / (float)tig_video_overlay_texture->w,
            (float)oh * 0.9f / (float)tig_video_overlay_texture->h);
        dst.w = (float)tig_video_overlay_texture->w * scale;
        dst.h = (float)tig_video_overlay_texture->h * scale;
        dst.x = ((float)ow - dst.w) / 2.0f;
        dst.y = ((float)oh - dst.h) / 2.0f;
        SDL_RenderTexture(renderer, tig_video_overlay_texture, NULL, &dst);
    }
    SDL_SetRenderLogicalPresentation(renderer, lw, lh, mode);
}
static bool tig_video_smooth_ui = true;

void tig_video_set_smooth_ui(bool enabled)
{
    tig_video_smooth_ui = enabled;
}
static SDL_Texture* tig_video_smooth_texture;
static void* tig_video_smooth_prev;
static size_t tig_video_smooth_prev_size;

void tig_video_set_smooth(int mode)
{
    tig_video_smooth = mode;
    tig_video_smooth_label_frames = 120;
    // Force a full re-filter next time it is switched on.
    if (tig_video_smooth_prev != NULL) {
        SDL_free(tig_video_smooth_prev);
        tig_video_smooth_prev = NULL;
        tig_video_smooth_prev_size = 0;
    }
}

int tig_video_get_smooth(void)
{
    return tig_video_smooth;
}

bool tig_video_has_xbrz(void)
{
#ifdef TIG_HAVE_XBRZ
    return true;
#else
    return false;
#endif
}

// Local tweak: automatic resolution. Width/Height 0 in HighRes/config.ini
// picks the logical resolution from the real (safe-area) window size: about
// 720 game rows, snapped to an integer scale when one is close, never below
// 800x600. A fixed scale (percent, e.g. 250 = x2.5) can be forced instead.
static int tig_video_scale_percent;

void tig_video_set_scale(int percent)
{
    tig_video_scale_percent = percent > 0 ? percent : 0;
}

void tig_video_logical_size_for(int pixel_width, int pixel_height, int percent, int* width_ptr, int* height_ptr)
{
    float k;
    float n;
    int width;
    int height;

    if (percent > 0) {
        k = (float)percent / 100.0f;
    } else {
        k = (float)pixel_height / 720.0f;
        n = SDL_roundf(k);
        if (n >= 1.0f && SDL_fabsf(k - n) <= 0.15f) {
            k = n;
        }
    }

    if (k < 1.0f) {
        k = 1.0f;
    }

    if (k == SDL_floorf(k)) {
        // Whole factor: round down so the frame scales by exactly k.
        width = pixel_width / (int)k;
        height = pixel_height / (int)k;
    } else {
        width = (int)((float)pixel_width / k + 0.5f);
        height = (int)((float)pixel_height / k + 0.5f);
    }

    if (height < 600) {
        width = (int)((float)pixel_width * 600.0f / (float)pixel_height + 0.5f);
        height = 600;
    }

    if (width < 800) {
        height = (int)((float)pixel_height * 800.0f / (float)pixel_width + 0.5f);
        width = 800;
        if (height < 600) {
            height = 600;
        }
    }

    *width_ptr = width;
    *height_ptr = height;
}

static bool tig_video_pixels_available;
static int tig_video_pixels_w;
static int tig_video_pixels_h;

bool tig_video_screen_pixels(int* width_ptr, int* height_ptr)
{
    if (!tig_video_pixels_available) {
        return false;
    }

    *width_ptr = tig_video_pixels_w;
    *height_ptr = tig_video_pixels_h;
    return true;
}

static void tig_video_measure_pixels(SDL_Window* window, bool fullscreen)
{
    int pw = 0;
    int ph = 0;

    if (fullscreen) {
        SDL_Rect safe;
        float density;

        SDL_SyncWindow(window);
        SDL_GetWindowSizeInPixels(window, &pw, &ph);

        // MacBook notch: the safe area is the part below the camera housing.
        density = SDL_GetWindowPixelDensity(window);
        if (SDL_GetWindowSafeArea(window, &safe) && safe.w > 0 && safe.h > 0 && density > 0.0f) {
            pw = (int)((float)safe.w * density + 0.5f);
            ph = (int)((float)safe.h * density + 0.5f);
        }
    } else {
        SDL_Rect usable;
        SDL_DisplayID display = SDL_GetDisplayForWindow(window);
        float density = SDL_GetWindowPixelDensity(window);

        if (display != 0 && SDL_GetDisplayUsableBounds(display, &usable) && density > 0.0f) {
            pw = (int)((float)usable.w * density + 0.5f);
            ph = (int)((float)usable.h * density + 0.5f);
        }
    }

    if (pw > 0 && ph > 0) {
        tig_video_pixels_available = true;
        tig_video_pixels_w = pw;
        tig_video_pixels_h = ph;
    }
}

static SDL_Texture* tig_video_smooth_frame(void)
{
    SDL_Surface* surface = tig_video_state.surface;
    int w = surface->w;
    int h = surface->h;

    if (tig_video_smooth_texture == NULL) {
        tig_video_smooth_texture = SDL_CreateTexture(tig_video_state.renderer,
            SDL_PIXELFORMAT_XRGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            w * 3,
            h * 3);
        if (tig_video_smooth_texture == NULL) {
            tig_video_smooth = 0;
            return NULL;
        }
        SDL_SetTextureScaleMode(tig_video_smooth_texture, SDL_SCALEMODE_LINEAR);
    }

    // Skip the filter when nothing changed since the last frame (unless
    // benchmarking with ARCANUM_FLIP_BENCH=1).
    size_t size = (size_t)surface->pitch * (size_t)h;
    static int bench = -1;
    if (bench < 0) {
        bench = SDL_getenv("ARCANUM_FLIP_BENCH") != NULL;
    }
    if (!bench && tig_video_smooth_prev != NULL
        && tig_video_smooth_prev_size == size
        && SDL_memcmp(tig_video_smooth_prev, surface->pixels, size) == 0) {
        return tig_video_smooth_texture;
    }

    void* pixels;
    int pitch;
    if (!SDL_LockTexture(tig_video_smooth_texture, NULL, &pixels, &pitch)) {
        tig_video_smooth = 0;
        return NULL;
    }
    const uint32_t* src = (const uint32_t*)surface->pixels;
    int src_pitch = surface->pitch / 4;
    uint32_t* dst = (uint32_t*)pixels;
    int dst_pitch = pitch / 4;
    if (tig_video_smooth == 3
        && !(tig_scalefx_init("HighRes/shaders") && tig_scalefx_run(src, src_pitch, w, h, dst, dst_pitch))) {
        // ScaleFX unavailable (no shaders / OpenGL): fall back to xBRZ.
        tig_video_smooth = 2;
        tig_video_smooth_label_frames = 120;
    }

    if (tig_video_smooth >= 2) {
        if (tig_video_smooth == 2) {
            tig_xbrz3x(src, src_pitch, w, h, dst, dst_pitch);
        }
        // Checkerboard dithering (see-through walls) stays as crisp blocks.
        for (int y = 1; y < h - 1; y++) {
            for (int x = 1; x < w - 1; x++) {
                if (tig_xbr_is_checker(src, src_pitch, w, h, x, y)) {
                    uint32_t p = src[(size_t)y * src_pitch + x] | 0xFF000000u;
                    for (int k = 0; k < 3; k++) {
                        uint32_t* d = dst + (size_t)(y * 3 + k) * dst_pitch + x * 3;
                        d[0] = p;
                        d[1] = p;
                        d[2] = p;
                    }
                }
            }
        }
    } else {
        tig_xbr3x(src, src_pitch, w, h, dst, dst_pitch);
    }

    // Keep the interface and its text crisp: plain 3x pixels over UI windows,
    // only the game world underneath gets smoothed.
    TigRect rects[64];
    int num_rects = tig_video_smooth_ui ? 0 : tig_window_overlay_rects(w, h, rects, 64);
    for (int r = 0; r < num_rects; r++) {
        int x0 = rects[r].x < 0 ? 0 : rects[r].x;
        int y0 = rects[r].y < 0 ? 0 : rects[r].y;
        int x1 = rects[r].x + rects[r].width > w ? w : rects[r].x + rects[r].width;
        int y1 = rects[r].y + rects[r].height > h ? h : rects[r].y + rects[r].height;
        for (int y = y0; y < y1; y++) {
            const uint32_t* s = src + (size_t)y * src_pitch;
            for (int k = 0; k < 3; k++) {
                uint32_t* d = dst + (size_t)(y * 3 + k) * dst_pitch;
                for (int x = x0; x < x1; x++) {
                    uint32_t p = s[x] | 0xFF000000u;
                    d[x * 3] = p;
                    d[x * 3 + 1] = p;
                    d[x * 3 + 2] = p;
                }
            }
        }
    }
    SDL_UnlockTexture(tig_video_smooth_texture);

    if (tig_video_smooth_prev_size != size) {
        SDL_free(tig_video_smooth_prev);
        tig_video_smooth_prev = SDL_malloc(size);
        tig_video_smooth_prev_size = tig_video_smooth_prev != NULL ? size : 0;
    }
    if (tig_video_smooth_prev != NULL) {
        SDL_memcpy(tig_video_smooth_prev, surface->pixels, size);
    }

    return tig_video_smooth_texture;
}

int tig_video_flip(void)
{
    // Local tweak: optional frame-cost log (ARCANUM_FLIP_BENCH=1).
    static int flip_bench = -1;
    static Uint64 flip_total;
    static int flip_count;
    if (flip_bench < 0) {
        flip_bench = SDL_getenv("ARCANUM_FLIP_BENCH") != NULL;
    }
    Uint64 flip_start = flip_bench ? SDL_GetTicksNS() : 0;

    SDL_Texture* smooth = NULL;
    if (tig_video_smooth && SDL_BYTESPERPIXEL(tig_video_state.surface->format) == 4) {
        smooth = tig_video_smooth_frame();
    }

    if (smooth == NULL) {
        SDL_UpdateTexture(tig_video_state.texture, NULL, tig_video_state.surface->pixels, tig_video_state.surface->pitch);
    }

    // Local tweak: letterbox bars must stay black (the filter label leaves the
    // draw colour white).
    SDL_SetRenderDrawColor(tig_video_state.renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(tig_video_state.renderer);
    SDL_RenderTexture(tig_video_state.renderer, smooth != NULL ? smooth : tig_video_state.texture, NULL, NULL);

    if (tig_fade_state.enabled) {
        SDL_BlendMode blend_mode;
        SDL_GetRenderDrawBlendMode(tig_video_state.renderer, &blend_mode);
        SDL_SetRenderDrawBlendMode(tig_video_state.renderer, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        SDL_SetRenderDrawColor(tig_video_state.renderer,
            tig_fade_state.color.r,
            tig_fade_state.color.g,
            tig_fade_state.color.b,
            tig_fade_state.color.a);
        SDL_RenderFillRect(tig_video_state.renderer, NULL);
        SDL_SetRenderDrawBlendMode(tig_video_state.renderer, blend_mode);
    }

    if (tig_video_smooth_label_frames > 0) {
        #ifdef TIG_HAVE_XBRZ
        static const char* labels[] = { "Filter: off", "Filter: xBR", "Filter: xBRZ", "Filter: ScaleFX (GPU)" };
#else
        static const char* labels[] = { "Filter: off", "Filter: xBR", "Filter: xBR (xBRZ not built)", "Filter: ScaleFX (GPU)" };
#endif
        tig_video_smooth_label_frames--;
        SDL_SetRenderDrawColor(tig_video_state.renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugText(tig_video_state.renderer, 8, 16, labels[tig_video_smooth % 4]);
    }

    tig_video_overlay_draw();

    if (tig_video_show_fps) {
        SDL_SetRenderDrawColor(tig_video_state.renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugTextFormat(tig_video_state.renderer, 0, 0, "%d", tig_video_state.fps);
    }

    if (flip_bench) {
        // Time the work up to the present call (vsync wait excluded).
        SDL_FlushRenderer(tig_video_state.renderer);
        flip_total += SDL_GetTicksNS() - flip_start;
        if (++flip_count == 300) {
            SDL_PropertiesID props = SDL_GetRendererProperties(tig_video_state.renderer);
            SDL_Log("Flip bench [%s, filter %d]: %.2f ms/frame",
                SDL_GetStringProperty(props, SDL_PROP_RENDERER_NAME_STRING, "?"),
                tig_video_smooth,
                (double)flip_total / flip_count / 1e6);
            flip_total = 0;
            flip_count = 0;
        }
    }

    SDL_RenderPresent(tig_video_state.renderer);

    return TIG_OK;
}

// 0x51F9E0
int tig_video_screenshot_set_settings(TigVideoScreenshotSettings* settings)
{
    int rc;

    if (tig_video_screenshot_key != -1) {
        rc = tig_message_set_key_handler(NULL, tig_video_screenshot_key);
        if (rc != TIG_OK) {
            return rc;
        }
    }

    tig_video_screenshot_key = settings->key;
    dword_6103A4 = settings->field_4;

    if (tig_video_screenshot_key != -1) {
        rc = tig_message_set_key_handler(tig_video_screenshot_make_internal, tig_video_screenshot_key);
        if (rc != TIG_OK) {
            return rc;
        }
    }

    return TIG_OK;
}

// 0x51FA30
int tig_video_screenshot_make(void)
{
    return tig_video_screenshot_make_internal(tig_video_screenshot_key);
}

// 0x51FAA0
int tig_video_get_palette(unsigned int* colors)
{
    if (!tig_video_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    memset(colors, 0, sizeof(*colors) * 256);

    return TIG_OK;
}

// 0x51FB10
int tig_video_3d_check_initialized(void)
{
    if (!tig_video_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    if (!tig_video_3d_initialized) {
        return TIG_ERR_GENERIC;
    }

    return TIG_OK;
}

// 0x51FB30
int tig_video_3d_check_hardware(void)
{
    if (!tig_video_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    if (!tig_video_3d_is_hardware) {
        return TIG_ERR_GENERIC;
    }

    return TIG_OK;
}

// 0x51FB50
int tig_video_3d_begin_scene(void)
{
    if (tig_video_3d_initialized) {
        return TIG_ERR_GENERIC;
    }

    tig_video_3d_scene_started = true;
    return TIG_OK;
}

// 0x51FBA0
int tig_video_3d_end_scene(void)
{
    if (!tig_video_3d_scene_started) {
        return TIG_ERR_GENERIC;
    }

    if (tig_video_3d_initialized) {
        return TIG_ERR_GENERIC;
    }

    tig_video_3d_scene_started = false;
    return TIG_OK;
}

// 0x51FC90
int tig_video_check_gamma_control(void)
{
    return TIG_OK;
}

// NOTE: The original code implements fading using DirectDraw gamma ramp
// adjustments which is not available in SDL3. This implementation achieves
// fading effect by drawing alpha-blended rectangle covering entire window in
// `tig_video_flip`.
//
// This implementation prefers `steps` over `duration` - the decision is based
// on subjective perception of default fade steps (48 steps) vs. default fade
// duration (2 seconds) on a target 60 fps monitor. The steps approach takes
// about 800 ms which is perfectly fine.
//
// 0x51FCA0
int tig_video_fade(tig_color_t color, int steps, float duration, TigFadeFlags flags)
{
    int step;

    (void)duration;

    if ((flags & TIG_FADE_IN) == 0) {
        // Enable faded state.
        tig_fade_state.enabled = true;
        tig_fade_state.color.r = (Uint8)tig_color_get_red(color);
        tig_fade_state.color.g = (Uint8)tig_color_get_green(color);
        tig_fade_state.color.b = (Uint8)tig_color_get_blue(color);
    }

    if ((flags & TIG_FADE_IN) != 0) {
        // Fade in by gradually reducing alpha from 255 (fully opaque) to 0
        // (completely transparent).
        for (step = 0; step < steps; step++) {
            tig_fade_state.color.a = (Uint8)(255 - step * 255 / steps);
            tig_video_flip();
        }
    } else {
        // Fade out by gradually increasing alpha from 0 (completely
        // transparent) to 255 (fully opaque).
        for (step = 0; step < steps; step++) {
            tig_fade_state.color.a = (Uint8)(step * 255 / steps);
            tig_video_flip();
        }
    }

    if ((flags & TIG_FADE_IN) != 0) {
        // Disable faded state.
        tig_fade_state.enabled = false;
    }

    return TIG_OK;
}

// 0x51FFE0
int tig_video_set_gamma(float gamma)
{
    if (gamma == tig_video_gamma) {
        return TIG_OK;
    }

    if (gamma < 0.0 || gamma >= 2.0) {
        return TIG_ERR_INVALID_PARAM;
    }

    tig_video_gamma = gamma;
    tig_video_fade(0, 0, 0.0, 1);

    return TIG_OK;
}

// 0x5200F0
int tig_video_buffer_create(TigVideoBufferCreateInfo* vb_create_info, TigVideoBuffer** video_buffer_ptr)
{
    TigVideoBuffer* video_buffer;
    int texture_width;
    int texture_height;

    video_buffer = (TigVideoBuffer*)MALLOC(sizeof(*video_buffer));
    memset(video_buffer, 0, sizeof(*video_buffer));
    *video_buffer_ptr = video_buffer;

    texture_width = vb_create_info->width;
    texture_height = vb_create_info->height;

    video_buffer->surface = SDL_CreateSurface(texture_width, texture_height, SDL_PIXELFORMAT_XRGB8888);
    if (video_buffer->surface == NULL) {
        return TIG_ERR_OUT_OF_MEMORY;
    }

    video_buffer->flags |= TIG_VIDEO_BUFFER_SYSTEM_MEMORY;

    if ((vb_create_info->flags & TIG_VIDEO_BUFFER_CREATE_COLOR_KEY) != 0) {
        video_buffer->flags |= TIG_VIDEO_BUFFER_COLOR_KEY;
        tig_video_buffer_set_color_key(*video_buffer_ptr, vb_create_info->color_key);
    }

    video_buffer->frame.x = 0;
    video_buffer->frame.y = 0;
    video_buffer->frame.width = vb_create_info->width;
    video_buffer->frame.height = vb_create_info->height;
    video_buffer->texture_width = texture_width;
    video_buffer->texture_height = texture_height;
    video_buffer->background_color = vb_create_info->background_color;

    if ((video_buffer->flags & TIG_VIDEO_BUFFER_TEXTURE) == 0) {
        SDL_FillSurfaceRect(video_buffer->surface, NULL, vb_create_info->background_color);
    }

    video_buffer->lock_count = 0;

    return TIG_OK;
}

// 0x520390
int tig_video_buffer_destroy(TigVideoBuffer* video_buffer)
{
    if (video_buffer == NULL) {
        return TIG_ERR_GENERIC;
    }

    SDL_DestroySurface(video_buffer->surface);
    FREE(video_buffer);

    return TIG_OK;
}

// 0x5203E0
int tig_video_buffer_data(TigVideoBuffer* video_buffer, TigVideoBufferData* video_buffer_data)
{
    if (video_buffer == NULL) {
        return TIG_ERR_GENERIC;
    }

    video_buffer_data->flags = video_buffer->flags;
    video_buffer_data->width = video_buffer->frame.width;
    video_buffer_data->height = video_buffer->frame.height;

    if ((video_buffer->flags & TIG_VIDEO_BUFFER_LOCKED) != 0) {
        video_buffer_data->pitch = video_buffer->surface->pitch;
    } else {
        video_buffer_data->pitch = 0;
    }

    video_buffer_data->background_color = video_buffer->background_color;
    video_buffer_data->color_key = video_buffer->color_key;
    video_buffer_data->bpp = tig_video_bpp;

    if ((video_buffer->flags & TIG_VIDEO_BUFFER_LOCKED) != 0) {
        video_buffer_data->pixels = video_buffer->surface->pixels;
    } else {
        video_buffer_data->pixels = NULL;
    }

    return TIG_OK;
}

// 0x520450
int tig_video_buffer_set_color_key(TigVideoBuffer* video_buffer, int color_key)
{
    if ((video_buffer->flags & TIG_VIDEO_BUFFER_COLOR_KEY) == 0) {
        return TIG_ERR_GENERIC;
    }

    if (!SDL_SetSurfaceColorKey(video_buffer->surface, true, color_key)) {
        return TIG_ERR_GENERIC;
    }

    video_buffer->color_key = color_key;

    return TIG_OK;
}

// 0x5204B0
int tig_video_buffer_lock(TigVideoBuffer* video_buffer)
{
    if (video_buffer->lock_count == 0) {
        if (!SDL_LockSurface(video_buffer->surface)) {
            return TIG_ERR_DIRECTX;
        }

        video_buffer->flags |= TIG_VIDEO_BUFFER_LOCKED;
    }

    video_buffer->lock_count++;

    return TIG_OK;
}

// 0x520500
int tig_video_buffer_unlock(TigVideoBuffer* video_buffer)
{
    if (video_buffer->lock_count == 1) {
        SDL_UnlockSurface(video_buffer->surface);
        video_buffer->flags &= ~TIG_VIDEO_BUFFER_LOCKED;
    }

    video_buffer->lock_count--;

    return TIG_OK;
}

// 0x520540
int tig_video_buffer_outline(TigVideoBuffer* video_buffer, TigRect* rect, tig_color_t color)
{
    TigRect line;
    int rc;

    line.x = rect->x;
    line.y = rect->y;
    line.width = rect->width;
    line.height = 1;
    rc = tig_video_buffer_fill(video_buffer, &line, color);
    if (rc != TIG_OK) {
        return rc;
    }

    line.x = rect->x;
    line.y = rect->y;
    line.width = 1;
    line.height = rect->height;
    rc = tig_video_buffer_fill(video_buffer, &line, color);
    if (rc != TIG_OK) {
        return rc;
    }

    line.x = rect->x + rect->width - 1;
    line.y = rect->y;
    line.width = 1;
    line.height = rect->height;
    rc = tig_video_buffer_fill(video_buffer, &line, color);
    if (rc != TIG_OK) {
        return rc;
    }

    line.x = rect->x;
    line.y = rect->y + rect->height - 1;
    line.width = rect->width;
    line.height = 1;
    rc = tig_video_buffer_fill(video_buffer, &line, color);
    if (rc != TIG_OK) {
        return rc;
    }

    return TIG_OK;
}

// 0x520630
int tig_video_buffer_fill(TigVideoBuffer* video_buffer, TigRect* rect, tig_color_t color)
{
    SDL_Rect native_rect;

    if (rect != NULL) {
        native_rect.x = rect->x;
        native_rect.y = rect->y;
        native_rect.w = rect->width;
        native_rect.h = rect->height;
    }

    if (!SDL_FillSurfaceRect(video_buffer->surface, rect != NULL ? &native_rect : NULL, color)) {
        return TIG_ERR_GENERIC;
    }

    return TIG_OK;
}

// 0x520660
int tig_video_buffer_line(TigVideoBuffer* video_buffer, TigLine* line, TigRect* a3, tig_color_t color)
{
    int pattern = 0;
    bool reversed;
    TigDrawLineModeInfo mode_info;
    TigDrawLineStyleInfo style_info;
    int dx;
    int dy;
    int step_x;
    int step_y;
    int error;
    int error_y;
    int error_x;
    int x1;
    int y1;
    int x2;
    int y2;
    int x;
    int y;

    (void)a3;

    if (tig_video_buffer_lock(video_buffer) != TIG_OK) {
        return TIG_ERR_DIRECTX;
    }

    if (line->y2 <= line->y1) {
        if (line->y2 < line->y1) {
            reversed = true;
        } else {
            reversed = line->x1 >= line->x2;
        }
    } else {
        reversed = false;
    }

    tig_draw_get_line_mode(&mode_info);
    tig_draw_get_line_style(&style_info);

    dx = abs(line->x2 - line->x1);
    dy = abs(line->y2 - line->y1);

    if (reversed) {
        if (dx < dy) {
            error_y = 2 * dx;
            error = 2 * dx - dy;
            error_x = 2 * (dx - dy);
        } else {
            error_y = 2 * dy;
            error = 2 * dy - dx;
            error_x = 2 * (dy - dx);
        }

        x1 = line->x2;
        y1 = line->y2;
        x2 = line->x1;
        y2 = line->y1;

        if (line->x1 < line->x2) {
            step_x = -1;
        } else if (line->x1 > line->x2) {
            step_x = 1;
        } else {
            step_x = 0;
        }
    } else {
        if (dx < dy) {
            error_y = 2 * dx;
            error = 2 * dx - dy;
            error_x = 2 * (dx - dy);
        } else {
            error_y = 2 * dy;
            error = 2 * dy - dx;
            error_x = 2 * (dy - dx);
        }

        x1 = line->x1;
        y1 = line->y1;
        x2 = line->x2;
        y2 = line->y2;

        if (line->x1 < line->x2) {
            step_x = 1;
        } else if (line->x1 > line->x2) {
            step_x = -1;
        } else {
            step_x = 0;
        }
    }

    switch (tig_video_bpp) {
    case 32:
        if (1) {
            uint32_t* dst = (uint32_t*)video_buffer->surface->pixels + (video_buffer->surface->pitch / 4) * y1 + x1;
            *dst = color;

            if (dx < dy) {
                y = y1;
                while (y != y2) {
                    if (error > 0) {
                        error += error_x;
                        y++;
                        dst += video_buffer->surface->pitch / 4 + step_x;
                    } else {
                        error += error_y;
                        y++;
                        dst += video_buffer->surface->pitch / 4;
                    }

                    switch (style_info.style) {
                    case TIG_LINE_STYLE_SOLID:
                        *dst = color;
                        break;
                    case TIG_LINE_STYLE_DOTTED:
                        if ((pattern & 1) != 0) {
                            *dst = color;
                        }
                        ++pattern;
                        break;
                    case TIG_LINE_STYLE_DASHED:
                        if (pattern < 3) {
                            *dst = color;
                        }
                        ++pattern;
                        if (pattern > 5) {
                            pattern = 0;
                        }
                        break;
                    }
                }
            } else {
                if (line->y1 == line->y2) {
                    step_y = step_x;
                } else {
                    step_y = step_x + video_buffer->surface->pitch / 4;
                }

                x = x1;
                while (x != x2) {
                    x += step_x;
                    if (error > 0) {
                        error += error_x;
                        dst += step_y;
                    } else {
                        error += error_y;
                        dst += step_x;
                    }

                    switch (style_info.style) {
                    case TIG_LINE_STYLE_SOLID:
                        *dst = (uint32_t)color;
                        break;
                    case TIG_LINE_STYLE_DOTTED:
                        if ((pattern & 1) != 0) {
                            *dst = (uint32_t)color;
                        }
                        ++pattern;
                        break;
                    case TIG_LINE_STYLE_DASHED:
                        if (pattern < 3) {
                            *dst = (uint32_t)color;
                        }
                        ++pattern;
                        if (pattern > 5) {
                            pattern = 0;
                        }
                        break;
                    }
                }
            }
        }
        break;
    }

    if (tig_video_buffer_unlock(video_buffer) != TIG_OK) {
        return TIG_ERR_GENERIC;
    }

    return TIG_OK;
}

// 0x520FB0
int sub_520FB0(TigVideoBuffer* video_buffer, unsigned int flags)
{
    TigVideoBufferData data;

    if (flags == 0) {
        return TIG_OK;
    }

    if (!tig_video_3d_initialized) {
        return TIG_ERR_GENERIC;
    }

    if (tig_video_buffer_data(video_buffer, &data) != TIG_OK) {
        return TIG_ERR_GENERIC;
    }

    if ((data.flags & TIG_VIDEO_BUFFER_RENDER_TARGET) == 0) {
        return TIG_ERR_GENERIC;
    }

    return TIG_OK;
}

// 0x521000
int tig_video_buffer_blit(TigVideoBufferBlitInfo* blit_info)
{
    bool stretched;
    float width_ratio;
    float height_ratio;
    TigRect bounds;
    TigRect blit_src_rect;
    TigRect blit_dst_rect;
    TigRect tmp_rect;
    SDL_Rect native_src_rect;
    SDL_Rect native_dst_rect;
    int rc;

    if (blit_info->src_rect->width == blit_info->dst_rect->width
        && blit_info->src_rect->height == blit_info->dst_rect->height) {
        stretched = false;

        // NOTE: Original code does not initialize these values, but we have
        // to keep compiler happy.
        width_ratio = 1;
        height_ratio = 1;
    } else {
        stretched = true;
        width_ratio = (float)blit_info->src_rect->width / (float)blit_info->dst_rect->width;
        height_ratio = (float)blit_info->src_rect->height / (float)blit_info->dst_rect->height;
    }

    bounds.x = 0;
    bounds.y = 0;
    bounds.width = blit_info->src_video_buffer->frame.width;
    bounds.height = blit_info->src_video_buffer->frame.height;

    rc = tig_rect_intersection(blit_info->src_rect,
        &bounds,
        &blit_src_rect);
    if (rc != TIG_OK) {
        return TIG_OK;
    }

    tmp_rect = *blit_info->dst_rect;

    if (stretched) {
        tmp_rect.x += (int)((float)(blit_src_rect.x - blit_info->src_rect->x) / width_ratio);
        tmp_rect.y += (int)((float)(blit_src_rect.y - blit_info->src_rect->y) / height_ratio);
        tmp_rect.width -= (int)((float)(blit_info->src_rect->width - blit_src_rect.width) / width_ratio);
        tmp_rect.height -= (int)((float)(blit_info->src_rect->height - blit_src_rect.height) / height_ratio);
    } else {
        tmp_rect.x += blit_src_rect.x - blit_info->src_rect->x;
        tmp_rect.y += blit_src_rect.y - blit_info->src_rect->y;
        tmp_rect.width -= blit_info->src_rect->width - blit_src_rect.width;
        tmp_rect.height -= blit_info->src_rect->height - blit_src_rect.height;
    }

    bounds.x = 0;
    bounds.y = 0;
    bounds.width = blit_info->dst_video_buffer->frame.width;
    bounds.height = blit_info->dst_video_buffer->frame.height;

    rc = tig_rect_intersection(&tmp_rect,
        &bounds,
        &blit_dst_rect);
    if (rc != TIG_OK) {
        return TIG_OK;
    }

    if (stretched) {
        blit_src_rect.x += (int)((float)(blit_dst_rect.x - tmp_rect.x) / width_ratio);
        blit_src_rect.y += (int)((float)(blit_dst_rect.y - tmp_rect.y) / height_ratio);
        blit_src_rect.width -= (int)((float)(tmp_rect.width - blit_dst_rect.width) / width_ratio);
        blit_src_rect.height -= (int)((float)(tmp_rect.height - blit_dst_rect.height) / height_ratio);
    } else {
        blit_src_rect.x += blit_dst_rect.x - tmp_rect.x;
        blit_src_rect.y += blit_dst_rect.y - tmp_rect.y;
        blit_src_rect.width -= tmp_rect.width - blit_dst_rect.width;
        blit_src_rect.height -= tmp_rect.height - blit_dst_rect.height;
    }

    native_src_rect.x = blit_src_rect.x;
    native_src_rect.y = blit_src_rect.y;
    native_src_rect.w = blit_src_rect.width;
    native_src_rect.h = blit_src_rect.height;

    native_dst_rect.x = blit_dst_rect.x;
    native_dst_rect.y = blit_dst_rect.y;
    native_dst_rect.w = blit_dst_rect.width;
    native_dst_rect.h = blit_dst_rect.height;

    if ((blit_info->flags & TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_LERP) != 0) {
        // 0x5221A2
        // The algorithm is adopted from `TIG_ART_BLT_BLEND_COLOR_LERP` blitter.
        // It is used to blit townmap tiles with partially obscured edges. These
        // tiles are loaded from a bunch of .bmp files, so it bypass art system
        // entirely.
        int tl_r = tig_color_get_red(blit_info->lerp_colors[0]);
        int tl_g = tig_color_get_green(blit_info->lerp_colors[0]);
        int tl_b = tig_color_get_blue(blit_info->lerp_colors[0]);

        int tr_r = tig_color_get_red(blit_info->lerp_colors[1]);
        int tr_g = tig_color_get_green(blit_info->lerp_colors[1]);
        int tr_b = tig_color_get_blue(blit_info->lerp_colors[1]);

        int br_r = tig_color_get_red(blit_info->lerp_colors[2]);
        int br_g = tig_color_get_green(blit_info->lerp_colors[2]);
        int br_b = tig_color_get_blue(blit_info->lerp_colors[2]);

        int bl_r = tig_color_get_red(blit_info->lerp_colors[3]);
        int bl_g = tig_color_get_green(blit_info->lerp_colors[3]);
        int bl_b = tig_color_get_blue(blit_info->lerp_colors[3]);

        float vert_start_step_r = (float)(bl_r - tl_r) / blit_info->lerp_rect->height;
        float vert_start_r = vert_start_step_r * (blit_src_rect.y - blit_info->lerp_rect->y) + tl_r;
        float vert_end_step_r = (float)(br_r - tr_r) / blit_info->lerp_rect->height;
        float vert_end_r = vert_end_step_r * (blit_src_rect.y - blit_info->lerp_rect->y) + tr_r;

        float vert_start_step_g = (float)(bl_g - tl_g) / blit_info->lerp_rect->height;
        float vert_start_g = vert_start_step_g * (blit_src_rect.y - blit_info->lerp_rect->y) + tl_g;
        float vert_end_step_g = (float)(br_g - tr_g) / blit_info->lerp_rect->height;
        float vert_end_g = vert_end_step_g * (blit_src_rect.y - blit_info->lerp_rect->y) + tr_g;

        float vert_start_step_b = (float)(bl_b - tl_b) / blit_info->lerp_rect->height;
        float vert_start_b = vert_start_step_b * (blit_src_rect.y - blit_info->lerp_rect->y) + tl_b;
        float vert_end_step_b = (float)(br_b - tr_b) / blit_info->lerp_rect->height;
        float vert_end_b = vert_end_step_b * (blit_src_rect.y - blit_info->lerp_rect->y) + tr_b;

        int x;
        int y;

        uint32_t* src = (uint32_t*)((uint8_t*)blit_info->src_video_buffer->surface->pixels
            + blit_info->src_video_buffer->surface->pitch * blit_src_rect.y
            + 4 * blit_src_rect.x);
        uint32_t* dst = (uint32_t*)((uint8_t*)blit_info->dst_video_buffer->surface->pixels
            + blit_info->dst_video_buffer->surface->pitch * blit_dst_rect.y
            + 4 * blit_dst_rect.x);

        for (y = 0; y < blit_dst_rect.height; ++y) {
            float hor_step_r = (vert_end_r - vert_start_r) / blit_info->lerp_rect->width;
            float hor_step_g = (vert_end_g - vert_start_g) / blit_info->lerp_rect->width;
            float hor_step_b = (vert_end_b - vert_start_b) / blit_info->lerp_rect->width;

            float r = vert_start_r + hor_step_r * (blit_src_rect.x - blit_info->lerp_rect->x);
            float g = vert_start_g + hor_step_g * (blit_src_rect.x - blit_info->lerp_rect->x);
            float b = vert_start_b + hor_step_b * (blit_src_rect.x - blit_info->lerp_rect->x);

            for (x = 0; x < blit_dst_rect.width; ++x) {
                if ((blit_info->src_video_buffer->flags & TIG_VIDEO_BUFFER_COLOR_KEY) == 0
                    || *src != blit_info->src_video_buffer->color_key) {
                    *dst = tig_color_mul(*src, tig_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b));
                }

                r += hor_step_r;
                g += hor_step_g;
                b += hor_step_b;

                src++;
                dst++;
            }

            vert_start_r += vert_start_step_r;
            vert_end_r += vert_end_step_r;

            vert_start_g += vert_start_step_g;
            vert_end_g += vert_end_step_g;

            vert_start_b += vert_start_step_b;
            vert_end_b += vert_end_step_b;

            src = (uint32_t*)((uint8_t*)src + blit_info->src_video_buffer->surface->pitch - 4 * blit_src_rect.width);
            dst = (uint32_t*)((uint8_t*)dst + blit_info->dst_video_buffer->surface->pitch - 4 * blit_dst_rect.width);
        }

        return TIG_OK;
    }

    if (stretched) {
        SDL_ScaleMode scale_mode = (blit_info->flags & TIG_VIDEO_BUFFER_BLIT_SCALE_LINEAR)
            ? SDL_SCALEMODE_LINEAR
            : SDL_SCALEMODE_NEAREST;

        if (!SDL_BlitSurfaceScaled(blit_info->src_video_buffer->surface, &native_src_rect, blit_info->dst_video_buffer->surface, &native_dst_rect, scale_mode)) {
            return TIG_ERR_GENERIC;
        }
    } else {
        if (!SDL_BlitSurface(blit_info->src_video_buffer->surface, &native_src_rect, blit_info->dst_video_buffer->surface, &native_dst_rect)) {
            return TIG_ERR_GENERIC;
        }
    }

    return TIG_OK;
}

// 0x522F30
int tig_video_buffer_get_pixel_color(TigVideoBuffer* video_buffer, int x, int y, unsigned int* color)
{
    int index;
    int rc;

    if (x < video_buffer->frame.x
        || y < video_buffer->frame.y
        || x >= video_buffer->frame.x + video_buffer->frame.width
        || y >= video_buffer->frame.y + video_buffer->frame.height) {
        return TIG_ERR_INVALID_PARAM;
    }

    rc = tig_video_buffer_lock(video_buffer);
    if (rc != TIG_OK) {
        return rc;
    }

    switch (tig_video_bpp) {
    case 32:
        index = y * video_buffer->surface->pitch + 4 * x;
        *color = *(uint32_t*)((uint8_t*)video_buffer->surface->pixels + index);
        break;
    }

    tig_video_buffer_unlock(video_buffer);

    return TIG_OK;
}

// 0x523120
int tig_video_buffer_tint(TigVideoBuffer* video_buffer, TigRect* rect, tig_color_t tint_color, TigVideoBufferTintMode mode)
{
    int rc;
    TigRect frame;
    int x;
    int y;

    if (mode >= TIG_VIDEO_BUFFER_TINT_MODE_COUNT) {
        return TIG_ERR_INVALID_PARAM;
    }

    if (tint_color == tig_color_make(0, 0, 0)
        && mode != TIG_VIDEO_BUFFER_TINT_MODE_GRAYSCALE) {
        return TIG_OK;
    }

    rc = tig_rect_intersection(rect, &(video_buffer->frame), &frame);
    if (rc != TIG_OK) {
        return rc;
    }

    rc = tig_video_buffer_lock(video_buffer);
    if (rc != TIG_OK) {
        return rc;
    }

    for (y = 0; y < frame.height; ++y) {
        switch (tig_video_bpp) {
        case 32:
            if (1) {
                uint32_t* dst = (uint32_t*)video_buffer->surface->pixels + (video_buffer->surface->pitch / 4) * (y + frame.y) + frame.x;
                uint32_t src_color;

                switch (mode) {
                case TIG_VIDEO_BUFFER_TINT_MODE_ADD:
                    for (x = 0; x < frame.width; ++x) {
                        src_color = *dst;
                        *dst++ = tig_color_add(tint_color, src_color);
                    }
                    break;
                case TIG_VIDEO_BUFFER_TINT_MODE_SUB:
                    for (x = 0; x < frame.width; ++x) {
                        src_color = *dst;
                        *dst++ = tig_color_sub(tint_color, src_color);
                    }
                    break;
                case TIG_VIDEO_BUFFER_TINT_MODE_MUL:
                    for (x = 0; x < frame.width; ++x) {
                        src_color = *dst;
                        *dst++ = tig_color_mul(tint_color, src_color);
                    }
                    break;
                case TIG_VIDEO_BUFFER_TINT_MODE_GRAYSCALE:
                    for (x = 0; x < frame.width; ++x) {
                        src_color = *dst;
                        *dst++ = tig_color_rgb_to_grayscale(src_color);
                    }
                    break;
                default:
                    // Should be unreachable.
                    abort();
                }
            }
            break;
        }
    }

    tig_video_buffer_unlock(video_buffer);
    return TIG_OK;
}

// 0x523930
int tig_video_buffer_save_to_bmp(TigVideoBufferSaveToBmpInfo* save_info)
{
    int rc;
    TigRect rect;

    if (save_info->rect != NULL) {
        rect = *save_info->rect;
    } else {
        rect.x = 0;
        rect.y = 0;
        rect.width = save_info->video_buffer->surface->w;
        rect.height = save_info->video_buffer->surface->h;
    }

    rc = tig_video_buffer_data_to_bmp(save_info->video_buffer->surface,
        &rect,
        save_info->path);

    return rc;
}

// 0x5239D0
int tig_video_buffer_load_from_bmp(const char* filename, TigVideoBuffer** video_buffer_ptr, unsigned int flags)
{
    SDL_IOStream* io;
    SDL_Surface* surface;
    SDL_Rect blit_rect;
    TigVideoBufferCreateInfo vb_create_info;
    int rc;

    io = tig_file_io_open(filename, "rb");
    if (io == NULL) {
        return TIG_ERR_IO;
    }

    surface = SDL_LoadBMP_IO(io, true);
    if (surface == NULL) {
        return TIG_ERR_IO;
    }

    if ((flags & 0x1) != 0) {
        vb_create_info.flags = TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY;
        vb_create_info.width = surface->w;
        vb_create_info.height = surface->h;
        vb_create_info.color_key = 0;
        vb_create_info.background_color = 0;

        rc = tig_video_buffer_create(&vb_create_info, video_buffer_ptr);
        if (rc != TIG_OK) {
            SDL_DestroySurface(surface);

            return rc;
        }
    }

    if ((*video_buffer_ptr)->surface->w < surface->w || (*video_buffer_ptr)->surface->h < surface->h) {
        if ((flags & 0x1) != 0) {
            tig_video_buffer_destroy(*video_buffer_ptr);
        }

        SDL_DestroySurface(surface);

        return TIG_ERR_INVALID_PARAM;
    }

    blit_rect.x = 0;
    blit_rect.y = 0;
    blit_rect.w = surface->w;
    blit_rect.h = surface->h;

    if (!SDL_BlitSurface(surface, &blit_rect, (*video_buffer_ptr)->surface, &blit_rect)) {
        if ((flags & 0x1) != 0) {
            tig_video_buffer_destroy(*video_buffer_ptr);
        }

        SDL_DestroySurface(surface);

        return TIG_ERR_GENERIC;
    }

    SDL_DestroySurface(surface);

    return TIG_OK;
}

// 0x524080
bool tig_video_window_create(TigInitInfo* init_info)
{
    const char* name = (init_info->flags & TIG_INITIALIZE_SET_WINDOW_NAME) != 0
        ? init_info->window_name
        : "TIG";

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if ((init_info->flags & TIG_INITIALIZE_WINDOWED) == 0) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    // Local tweak: Width/Height 0 = automatic; create the window with a
    // placeholder size and work out the real one once it exists.
    bool auto_size = init_info->width <= 0 || init_info->height <= 0;
    if (auto_size) {
        init_info->width = 1280;
        init_info->height = 720;
    }

    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

    int window_width = (int)(init_info->width * scale);
    int window_height = (int)(init_info->height * scale);

#if SDL_PLATFORM_ANDROID || SDL_PLATFORM_IOS
    SDL_Rect display_bounds;
    SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &display_bounds);

    // Calculate the view size in logical pixels. Android reports display bounds
    // in physical pixels and provides the appropriate scale, while in iOS
    // display bounds are already in logical pixels with a scale of 1.0.
    init_info->width = (int)((float)display_bounds.w / scale + 0.5f);
    init_info->height = (int)((float)display_bounds.h / scale + 0.5f);

    const int min_height = 600;
    if (init_info->height < min_height) {
        // The logical resolution is too small to accommodate the base graphic.
        // This likely means we're on a mobile phone, where the height is
        // usually between 350 and 450 logical pixels.
        float width_scale = (float)min_height / (float)init_info->height;
        init_info->width = (int)((float)init_info->width * width_scale + 0.5f);
        init_info->height = min_height;
    }

    window_width = display_bounds.w;
    window_height = display_bounds.h;
    flags |= SDL_WINDOW_FULLSCREEN;
#endif

// We're using streaming texture which is extremely slow in Metal.
#if SDL_PLATFORM_MACOS
    // Local tweak: exclusive (non-Spaces) fullscreen hides the menu bar and
    // Dock for good, so pushing the cursor to the top edge to scroll the map
    // no longer reveals the menu bar and steals the mouse.
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl,metal");
#elif SDL_PLATFORM_IOS
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2,metal");
#endif

    SDL_Window* window;
    SDL_Renderer* renderer;
    if (!SDL_CreateWindowAndRenderer(name, window_width, window_height, flags, &window, &renderer)) {
        return false;
    }

    SDL_PropertiesID renderer_props = SDL_GetRendererProperties(renderer);
    const char* driver_name = SDL_GetStringProperty(renderer_props, SDL_PROP_RENDERER_NAME_STRING, "");
    tig_debug_printf("TIG Video: Using '%s' driver\n", driver_name);

    // Local tweak: keep the cursor inside the game window in fullscreen so
    // edge scrolling cannot run off to another display or the screen corner.
    if ((flags & SDL_WINDOW_FULLSCREEN) != 0) {
        SDL_SetWindowMouseGrab(window, true);
    }

    if ((init_info->flags & TIG_INITIALIZE_POSITIONED) != 0) {
        SDL_SetWindowPosition(window, init_info->x, init_info->y);
    }

#if !SDL_PLATFORM_ANDROID && !SDL_PLATFORM_IOS
    tig_video_measure_pixels(window, (flags & SDL_WINDOW_FULLSCREEN) != 0);

    if (auto_size && tig_video_pixels_available) {
        tig_video_logical_size_for(tig_video_pixels_w,
            tig_video_pixels_h,
            tig_video_scale_percent,
            &(init_info->width),
            &(init_info->height));

        if ((flags & SDL_WINDOW_FULLSCREEN) == 0) {
            // Windowed: the largest whole multiple of the logical size that
            // fits the usable desktop (at least 1x).
            float density = SDL_GetWindowPixelDensity(window);
            int fit = SDL_min(tig_video_pixels_w / init_info->width, tig_video_pixels_h / init_info->height);
            if (fit < 1) {
                fit = 1;
            }
            if (density <= 0.0f) {
                density = 1.0f;
            }
            SDL_SetWindowSize(window,
                (int)((float)(init_info->width * fit) / density),
                (int)((float)(init_info->height * fit) / density));
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }

        tig_debug_printf("TIG Video: automatic resolution %dx%d for %dx%d pixels\n",
            init_info->width,
            init_info->height,
            tig_video_pixels_w,
            tig_video_pixels_h);
    }
#endif

    if (!SDL_SetRenderVSync(renderer, 1)) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return false;
    }

    if (!SDL_SetRenderLogicalPresentation(renderer, init_info->width, init_info->height, SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return false;
    }

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, init_info->width, init_info->height);
    if (texture == NULL) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return false;
    }

    // Local tweak: upscale the low-res frame sharply (no linear blur, no
    // uneven nearest-neighbour pixels on non-integer scale factors).
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_PIXELART);

    SDL_PropertiesID texture_props = SDL_GetTextureProperties(texture);
    SDL_PixelFormat format = (SDL_PixelFormat)SDL_GetNumberProperty(texture_props, SDL_PROP_TEXTURE_FORMAT_NUMBER, 0);

    SDL_Surface* surface = SDL_CreateSurface(init_info->width, init_info->height, format);
    if (surface == NULL) {
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return false;
    }

    tig_video_state.window = window;
    tig_video_state.renderer = renderer;
    tig_video_state.texture = texture;
    tig_video_state.surface = surface;

    stru_610388.x = 0;
    stru_610388.y = 0;
    stru_610388.width = init_info->width;
    stru_610388.height = init_info->height;

    return true;
}

// 0x5242F0
void tig_video_window_destroy(void)
{
    if (tig_video_state.surface != NULL) {
        SDL_DestroySurface(tig_video_state.surface);
        tig_video_state.surface = NULL;
    }

    if (tig_video_smooth_texture != NULL) {
        SDL_DestroyTexture(tig_video_smooth_texture);
        tig_video_smooth_texture = NULL;
    }
    SDL_free(tig_video_smooth_prev);
    tig_video_smooth_prev = NULL;
    tig_video_smooth_prev_size = 0;
    tig_scalefx_exit();

    if (tig_video_state.texture != NULL) {
        SDL_DestroyTexture(tig_video_state.texture);
        tig_video_state.texture = NULL;
    }

    if (tig_video_state.renderer != NULL) {
        SDL_DestroyRenderer(tig_video_state.renderer);
        tig_video_state.renderer = NULL;
    }

    if (tig_video_state.window != NULL) {
        SDL_DestroyWindow(tig_video_state.window);
        tig_video_state.window = NULL;
    }
}

// 0x524830
bool sub_524830(void)
{
    int bpp;
    Uint32 r;
    Uint32 g;
    Uint32 b;
    Uint32 a;

    if (!SDL_GetMasksForPixelFormat(tig_video_state.surface->format, &bpp, &r, &g, &b, &a)) {
        return false;
    }

    if (tig_color_set_rgba_settings(r, g, b, a) != TIG_OK) {
        return false;
    }

    return true;
}

// 0x525250
int tig_video_screenshot_make_internal(int key)
{
    int rc;
    int index;
    char path[TIG_MAX_PATH];
    TigRect rect;

    if (tig_video_screenshot_key != key) {
        return TIG_ERR_GENERIC;
    }

    for (index = 0; index < INT_MAX; index++) {
        SDL_snprintf(path, sizeof(path), "screen%04d.bmp", index);
        if (!tig_file_exists(path, NULL)) {
            break;
        }
    }

    if (index == INT_MAX) {
        return TIG_ERR_IO;
    }

    rect.x = 0;
    rect.y = 0;
    rect.width = tig_video_state.surface->w;
    rect.height = tig_video_state.surface->h;

    rc = tig_video_buffer_data_to_bmp(tig_video_state.surface, &rect, path);

    return rc;
}

// 0x525ED0
int tig_video_buffer_data_to_bmp(SDL_Surface* surface, TigRect* rect, const char* file_name)
{
    SDL_Surface* intermediate_surface;
    SDL_IOStream* io;
    int rc;

    io = tig_file_io_open(file_name, "wb");
    if (io == NULL) {
        return TIG_ERR_IO;
    }

    if (rect->x == 0
        && rect->y == 0
        && rect->width == surface->w
        && rect->height == surface->h) {
        intermediate_surface = surface;
    } else {
        SDL_Rect blit_rect;

        intermediate_surface = SDL_CreateSurface(rect->width, rect->height, surface->format);
        if (intermediate_surface == NULL) {
            SDL_CloseIO(io);
            return TIG_ERR_GENERIC;
        }

        blit_rect.x = rect->x;
        blit_rect.y = rect->y;
        blit_rect.w = rect->width;
        blit_rect.h = rect->height;

        if (!SDL_BlitSurface(surface, &blit_rect, intermediate_surface, &blit_rect)) {
            SDL_DestroySurface(intermediate_surface);
            SDL_CloseIO(io);
            return TIG_ERR_GENERIC;
        }
    }

    rc = TIG_OK;
    if (!SDL_SaveBMP_IO(intermediate_surface, io, true)) {
        rc = TIG_ERR_IO;
    }

    if (intermediate_surface != surface) {
        SDL_DestroySurface(intermediate_surface);
    }

    return rc;
}
