#include "tig/gamepad.h"
#include "tig/gamepad_script.h"

#include <math.h>
#include <stdlib.h>

#include "tig/button.h"
#include "tig/core.h"
#include "tig/debug.h"
#include "tig/kb.h"
#include "tig/mouse.h"
#include "tig/video.h"
#include "tig/window.h"

// Local tweak: gamepad as a virtual mouse and keyboard.
//
// DualSense layout (SDL names in brackets):
//   left stick          cursor; press and hold for slow, precise movement
//   touchpad            cursor like a laptop trackpad; click = left click
//   right stick         pan the map; press = centre on the player (Home)
//   cross   [SOUTH]     left click (hold to drag)
//   circle  [EAST]      on the map: right click; in windows: back (Esc)
//   square  [WEST]      on the map: highlight objects while held (Left
//                       Alt); in windows: right click
//   triangle[NORTH]     turn-based combat on/off, end turn (Space)
//   R2                  combat mode (R)
//   d-pad               up = character (C), right = world map (W),
//                       down = schematics (T), left = inventory (I)
//   L2 + d-pad          keys 1-4 (hotkey bar, dialogue replies)
//   L1 + d-pad          keys 5-8;  L1 tap = skills (K)
//   R1 + d-pad up/down  keys 9/0;  R1 tap = spells (M)
//   hold L1/L2/R1 or PS shows the button chart (HighRes/gamepad.bmp)
//   Options [START]     Esc (game menu)
//   Create  [BACK]      quick save (F7); hold for a second = quick load (F8)

#define MAX_GAMEPADS 4
#define STICK_DEADZONE 0.15f
#define TRIGGER_THRESHOLD 0.5f
#define PRECISION_FACTOR 0.3f
#define QUICKLOAD_HOLD_MS 1000
#define HINT_HOLD_MS 400
#define STICKY_FACTOR 0.4f
#define STICKY_STOP_MS 120

static bool tig_gamepad_enabled = true;
static float tig_gamepad_cursor_speed = 900.0f;
static float tig_gamepad_pan_speed = 1100.0f;

static SDL_Gamepad* tig_gamepad_pads[MAX_GAMEPADS];

static Uint64 tig_gamepad_last_ping_ns;
static float tig_gamepad_cursor_rest_x;
static float tig_gamepad_cursor_rest_y;
static float tig_gamepad_pan_acc_x;
static float tig_gamepad_pan_acc_y;

static bool tig_gamepad_precision;
static bool tig_gamepad_l1_down;
static bool tig_gamepad_l1_used;
static bool tig_gamepad_r1_down;
static bool tig_gamepad_r1_used;
static bool tig_gamepad_l2_down;
static bool tig_gamepad_r2_down;
static bool tig_gamepad_guide_down;
static bool (*tig_gamepad_sticky_fn)(void);
static bool (*tig_gamepad_on_map_fn)(void);
static bool (*tig_gamepad_dpad_fn)(int direction);
static int (*tig_gamepad_targets_fn)(TigRect* rects, int max);
static void (*tig_gamepad_cycle_fn)(void);
static Uint64 tig_gamepad_west_down_ms;
// True while the gamepad (not a real mouse) is what the player uses.
static bool tig_gamepad_in_use;
static tig_window_handle_t tig_gamepad_last_top = TIG_WINDOW_HANDLE_INVALID;
static Uint64 tig_gamepad_snap_at_ms;
static bool tig_gamepad_east_is_back;
static bool tig_gamepad_west_is_click;
static bool tig_gamepad_sticky_prev;
static Uint64 tig_gamepad_sticky_stop_until_ms;
static Uint64 tig_gamepad_modifier_since_ms;
static bool tig_gamepad_hint_shown;
static Uint64 tig_gamepad_back_down_ms;
static bool tig_gamepad_back_fired;

static bool tig_gamepad_touch_active;
static float tig_gamepad_touch_x;
static float tig_gamepad_touch_y;

static void tig_gamepad_open(SDL_JoystickID id);
static void tig_gamepad_close(SDL_JoystickID id);
static void tig_gamepad_button(SDL_GamepadButton button, bool down);
static void tig_gamepad_key(SDL_Scancode scancode, bool down);
static void tig_gamepad_tap(SDL_Scancode scancode);
static void tig_gamepad_move_cursor(float dx, float dy);
static bool tig_gamepad_screen_size(int* width, int* height);
static float tig_gamepad_axis(SDL_GamepadAxis axis);
static void tig_gamepad_stick(SDL_GamepadAxis axis_x, SDL_GamepadAxis axis_y, float* x, float* y);

void tig_gamepad_set_enabled(bool enabled)
{
    tig_gamepad_enabled = enabled;
}

void tig_gamepad_set_cursor_speed(int pixels_per_second)
{
    if (pixels_per_second > 0) {
        tig_gamepad_cursor_speed = (float)pixels_per_second;
    }
}

void tig_gamepad_set_pan_speed(int pixels_per_second)
{
    if (pixels_per_second > 0) {
        tig_gamepad_pan_speed = (float)pixels_per_second;
    }
}

void tig_gamepad_set_cycle_callback(void (*fn)(void))
{
    tig_gamepad_cycle_fn = fn;
}

void tig_gamepad_set_targets_callback(int (*fn)(TigRect* rects, int max))
{
    tig_gamepad_targets_fn = fn;
}

// A new window came up while playing with the gamepad: put the cursor on its
// first button (reading order), so cross never fires whatever happened to be
// under the cursor (the main menu opens with "Exit game" there).
static void tig_gamepad_focus_first(void)
{
    TigRect targets[256];
    int width;
    int height;
    int count;
    int index;
    int best = -1;
    tig_window_handle_t top = tig_window_top_with_buttons();

    if (top == TIG_WINDOW_HANDLE_INVALID || !tig_gamepad_screen_size(&width, &height)) {
        return;
    }

    count = tig_button_focus_targets(targets, 256, width, height, top);
    for (index = 0; index < count; index++) {
        if (best == -1
            || targets[index].y < targets[best].y - 8
            || (abs(targets[index].y - targets[best].y) <= 8 && targets[index].x < targets[best].x)) {
            best = index;
        }
    }

    if (best != -1) {
        tig_debug_printf("Gamepad: first focus %d,%d\n", targets[best].x, targets[best].y);
        tig_mouse_set_position(targets[best].x, targets[best].y);
    }
}

// Focus navigation: put the cursor on the nearest clickable thing in the
// d-pad direction (0 up, 1 right, 2 down, 3 left).
static bool tig_gamepad_focus_move(int direction)
{
    static const int dirs[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
    TigRect targets[512];
    TigMouseState mouse_state;
    int count;
    int width;
    int height;
    int index;
    int best = -1;
    float best_score = 0.0f;

    if (tig_mouse_get_state(&mouse_state) != TIG_OK
        || !tig_gamepad_screen_size(&width, &height)) {
        return false;
    }

    count = tig_button_focus_targets(targets, 512, width, height, TIG_WINDOW_HANDLE_INVALID);
    if (tig_gamepad_targets_fn != NULL) {
        count += tig_gamepad_targets_fn(targets + count, 512 - count);
    }

    for (index = 0; index < count; index++) {
        float dx = (float)(targets[index].x - mouse_state.x);
        float dy = (float)(targets[index].y - mouse_state.y);
        float along = dx * (float)dirs[direction][0] + dy * (float)dirs[direction][1];
        float across = fabsf(dx * (float)dirs[direction][1] - dy * (float)dirs[direction][0]);
        float half_across = (float)(dirs[direction][0] != 0 ? targets[index].height : targets[index].width) / 2.0f;
        float half_along = (float)(dirs[direction][0] != 0 ? targets[index].width : targets[index].height) / 2.0f;
        float score;

        // Skip what the cursor is already on and what lies behind it.
        if (along <= half_along * 0.5f + 2.0f) {
            continue;
        }

        // Things straight ahead (overlapping the cursor's line) win over
        // things off to the side.
        across = across > half_across ? across - half_across : 0.0f;
        score = along + across * 3.0f;
        if (best == -1 || score < best_score) {
            best = index;
            best_score = score;
        }
    }

    if (best == -1) {
        return false;
    }

    tig_debug_printf("Gamepad: focus %d,%d (%dx%d)\n",
        targets[best].x, targets[best].y, targets[best].width, targets[best].height);
    tig_mouse_set_position(targets[best].x, targets[best].y);
    return true;
}

void tig_gamepad_set_dpad_callback(bool (*fn)(int direction))
{
    tig_gamepad_dpad_fn = fn;
}

void tig_gamepad_set_on_map_callback(bool (*fn)(void))
{
    tig_gamepad_on_map_fn = fn;
}

void tig_gamepad_set_sticky_callback(bool (*fn)(void))
{
    tig_gamepad_sticky_fn = fn;
}

bool tig_gamepad_handle_event(const SDL_Event* event)
{
    switch (event->type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        tig_gamepad_open(event->gdevice.which);
        return true;
    case SDL_EVENT_GAMEPAD_REMOVED:
        tig_gamepad_close(event->gdevice.which);
        return true;
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        // A real mouse is in use: stop moving its cursor around.
        tig_gamepad_in_use = false;
        return false;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (tig_gamepad_enabled) {
            tig_gamepad_in_use = true;
            tig_set_active(true);
            tig_debug_printf("Gamepad: button %s %s (L1 %d L2 %d R1 %d)\n",
                SDL_GetGamepadStringForButton((SDL_GamepadButton)event->gbutton.button),
                event->gbutton.down ? "down" : "up",
                tig_gamepad_l1_down, tig_gamepad_l2_down, tig_gamepad_r1_down);
            tig_gamepad_button((SDL_GamepadButton)event->gbutton.button, event->gbutton.down);
        }
        return true;
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
        if (event->gtouchpad.finger == 0) {
            tig_gamepad_touch_active = true;
            tig_gamepad_touch_x = event->gtouchpad.x;
            tig_gamepad_touch_y = event->gtouchpad.y;
        }
        return true;
    case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
        if (event->gtouchpad.finger == 0 && tig_gamepad_touch_active && tig_gamepad_enabled) {
            int width;
            int height;
            float dx = event->gtouchpad.x - tig_gamepad_touch_x;
            float dy = event->gtouchpad.y - tig_gamepad_touch_y;

            tig_gamepad_touch_x = event->gtouchpad.x;
            tig_gamepad_touch_y = event->gtouchpad.y;

            if (tig_gamepad_screen_size(&width, &height)) {
                // The pad is about twice as wide as it is tall: a full swipe
                // across it covers the screen width, the same scale is used
                // vertically.
                float scale = (float)width * (tig_gamepad_precision ? PRECISION_FACTOR : 1.0f);
                tig_set_active(true);
                tig_gamepad_move_cursor(dx * scale, dy * scale * 0.5f);
            }
        }
        return true;
    case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
        if (event->gtouchpad.finger == 0) {
            tig_gamepad_touch_active = false;
        }
        return true;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    case SDL_EVENT_GAMEPAD_REMAPPED:
    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
    case SDL_EVENT_GAMEPAD_UPDATE_COMPLETE:
        // Sticks and triggers are polled in tig_gamepad_ping.
        return true;
    default:
        return false;
    }
}

void tig_gamepad_ping(void)
{
    Uint64 now_ns;
    float dt;
    float x;
    float y;
    bool l2;
    bool r2;

    tig_gamepad_script_ping();

    now_ns = SDL_GetTicksNS();
    if (tig_gamepad_last_ping_ns == 0) {
        tig_gamepad_last_ping_ns = now_ns;
        return;
    }

    dt = (float)(now_ns - tig_gamepad_last_ping_ns) / 1e9f;
    tig_gamepad_last_ping_ns = now_ns;

    // Pings may stall (loading, movies); never jump the cursor across the
    // screen because of a long gap.
    if (dt > 0.1f) {
        dt = 0.1f;
    }

    if (!tig_gamepad_enabled || tig_gamepad_pads[0] == NULL) {
        return;
    }

    // Left stick: cursor. Squared response gives fine control near the
    // centre and full speed at the edge.
    tig_gamepad_stick(SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, &x, &y);
    if (x != 0.0f || y != 0.0f) {
        float mag = sqrtf(x * x + y * y);

        tig_gamepad_in_use = true;
        float speed = tig_gamepad_cursor_speed * mag * (tig_gamepad_precision ? PRECISION_FACTOR : 1.0f);
        bool sticky = tig_gamepad_sticky_fn != NULL && tig_gamepad_sticky_fn();

        // Sticky targets: stop for a moment on reaching one (unless the
        // stick is pushed all the way, i.e. the player is sweeping past),
        // then move slower while over it.
        if (sticky && !tig_gamepad_sticky_prev && mag < 0.95f) {
            tig_gamepad_sticky_stop_until_ms = SDL_GetTicks() + STICKY_STOP_MS;
        }
        tig_gamepad_sticky_prev = sticky;
        if (sticky) {
            speed *= STICKY_FACTOR;
        }

        tig_set_active(true);
        if (SDL_GetTicks() >= tig_gamepad_sticky_stop_until_ms) {
            tig_gamepad_move_cursor(x * speed * dt, y * speed * dt);
        }
    } else {
        tig_gamepad_sticky_prev = tig_gamepad_sticky_fn != NULL && tig_gamepad_sticky_fn();
    }

    // Right stick: pan the map. scroll_by() moves the view content, so the
    // signs are inverted: stick right = look right = content moves left.
    tig_gamepad_stick(SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY, &x, &y);
    if ((x != 0.0f || y != 0.0f)
        && tig_gamepad_on_map_fn != NULL
        && !tig_gamepad_on_map_fn()) {
        // In windows the right stick is a mouse wheel (lists, text).
        static float wheel_rest;
        int steps;

        tig_gamepad_in_use = true;
        tig_set_active(true);
        wheel_rest += -y * fabsf(y) * 12.0f * dt;
        steps = (int)wheel_rest;
        if (steps != 0) {
            wheel_rest -= (float)steps;
            tig_mouse_wheel(0, steps);
        }
    }
    // The pan is always gathered: the game uses it on the map and for the
    // open world/town map, and drops it elsewhere.
    if (x != 0.0f || y != 0.0f) {
        float mag = sqrtf(x * x + y * y);

        tig_set_active(true);
        // Cubic response: a gentle start, full speed only at full tilt.
        tig_gamepad_pan_acc_x -= x * mag * mag * tig_gamepad_pan_speed * dt;
        tig_gamepad_pan_acc_y -= y * mag * mag * tig_gamepad_pan_speed * dt;
    }

    // Triggers behave like buttons.
    l2 = tig_gamepad_axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > TRIGGER_THRESHOLD;
    if (l2 != tig_gamepad_l2_down) {
        tig_gamepad_l2_down = l2;
    }

    r2 = tig_gamepad_axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > TRIGGER_THRESHOLD;
    if (r2 != tig_gamepad_r2_down) {
        tig_gamepad_r2_down = r2;
        if (r2) {
            tig_set_active(true);
            tig_gamepad_tap(SDL_SCANCODE_R);
        }
    }

    // Follow newly opened windows with the focus.
    {
        tig_window_handle_t top = tig_window_top_with_buttons();
        if (top != tig_gamepad_last_top) {
            tig_gamepad_last_top = top;
            if (tig_gamepad_in_use && tig_gamepad_on_map_fn != NULL && !tig_gamepad_on_map_fn()) {
                // Buttons are often added right after the window.
                tig_gamepad_snap_at_ms = SDL_GetTicks() + 150;
            }
        }
        if (tig_gamepad_snap_at_ms != 0 && SDL_GetTicks() >= tig_gamepad_snap_at_ms) {
            tig_gamepad_snap_at_ms = 0;
            tig_gamepad_focus_first();
        }
    }

    // Holding a modifier (or PS) for a moment shows the button chart.
    {
        bool modifier = tig_gamepad_l1_down || tig_gamepad_l2_down || tig_gamepad_r1_down;
        bool hint;

        if (!modifier) {
            tig_gamepad_modifier_since_ms = 0;
        } else if (tig_gamepad_modifier_since_ms == 0) {
            tig_gamepad_modifier_since_ms = SDL_GetTicks();
        }

        hint = tig_gamepad_guide_down
            || (modifier && SDL_GetTicks() - tig_gamepad_modifier_since_ms >= HINT_HOLD_MS);
        if (hint != tig_gamepad_hint_shown) {
            tig_gamepad_hint_shown = hint;
            tig_video_set_overlay_visible(hint);
        }
    }

    // Create held long enough: quick load.
    if (tig_gamepad_back_down_ms != 0
        && !tig_gamepad_back_fired
        && SDL_GetTicks() - tig_gamepad_back_down_ms >= QUICKLOAD_HOLD_MS) {
        tig_gamepad_back_fired = true;
        tig_gamepad_tap(SDL_SCANCODE_F8);
    }
}

void tig_gamepad_pan_take(float* dx, float* dy)
{
    *dx = tig_gamepad_pan_acc_x;
    *dy = tig_gamepad_pan_acc_y;
    tig_gamepad_pan_acc_x = 0.0f;
    tig_gamepad_pan_acc_y = 0.0f;
}

static void tig_gamepad_open(SDL_JoystickID id)
{
    int index;

    for (index = 0; index < MAX_GAMEPADS; index++) {
        if (tig_gamepad_pads[index] != NULL
            && SDL_GetGamepadID(tig_gamepad_pads[index]) == id) {
            return;
        }
    }

    for (index = 0; index < MAX_GAMEPADS; index++) {
        if (tig_gamepad_pads[index] == NULL) {
            tig_gamepad_pads[index] = SDL_OpenGamepad(id);
            if (tig_gamepad_pads[index] != NULL) {
                tig_debug_printf("Gamepad connected: %s\n", SDL_GetGamepadName(tig_gamepad_pads[index]));
            } else {
                tig_debug_printf("Gamepad: cannot open %d: %s\n", (int)id, SDL_GetError());
            }
            return;
        }
    }
}

static void tig_gamepad_close(SDL_JoystickID id)
{
    int index;

    for (index = 0; index < MAX_GAMEPADS; index++) {
        if (tig_gamepad_pads[index] != NULL
            && SDL_GetGamepadID(tig_gamepad_pads[index]) == id) {
            tig_debug_printf("Gamepad disconnected: %s\n", SDL_GetGamepadName(tig_gamepad_pads[index]));
            SDL_CloseGamepad(tig_gamepad_pads[index]);
            tig_gamepad_pads[index] = NULL;
        }
    }

    // Keep the first slot filled so polling has a pad to read.
    for (index = 1; index < MAX_GAMEPADS; index++) {
        if (tig_gamepad_pads[0] == NULL && tig_gamepad_pads[index] != NULL) {
            tig_gamepad_pads[0] = tig_gamepad_pads[index];
            tig_gamepad_pads[index] = NULL;
        }
    }

    // Release anything a vanished pad was holding.
    tig_gamepad_precision = false;
    tig_gamepad_l1_down = tig_gamepad_r1_down = false;
    tig_gamepad_l2_down = tig_gamepad_r2_down = false;
    tig_gamepad_back_down_ms = 0;
    tig_gamepad_touch_active = false;
}

static void tig_gamepad_button(SDL_GamepadButton button, bool down)
{
    static const SDL_Scancode dpad_keys[4][4] = {
        // up, right, down, left
        { SDL_SCANCODE_C, SDL_SCANCODE_W, SDL_SCANCODE_T, SDL_SCANCODE_I },
        { SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4 },
        { SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8 },
        { SDL_SCANCODE_9, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_0, SDL_SCANCODE_UNKNOWN },
    };
    int dpad;
    int layer;

    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
    case SDL_GAMEPAD_BUTTON_TOUCHPAD:
        tig_mouse_set_button(TIG_MOUSE_BUTTON_LEFT, down);
        break;
    case SDL_GAMEPAD_BUTTON_EAST:
        // The release goes wherever the press went, even if a window opened
        // or closed in between.
        if (down) {
            tig_gamepad_east_is_back = tig_gamepad_on_map_fn != NULL && !tig_gamepad_on_map_fn();
        }
        if (tig_gamepad_east_is_back) {
            if (down) {
                tig_gamepad_tap(SDL_SCANCODE_ESCAPE);
            }
        } else {
            tig_mouse_set_button(TIG_MOUSE_BUTTON_RIGHT, down);
        }
        break;
    case SDL_GAMEPAD_BUTTON_WEST:
        if (down) {
            tig_gamepad_west_is_click = tig_gamepad_on_map_fn != NULL && !tig_gamepad_on_map_fn();
        }
        if (tig_gamepad_west_is_click) {
            tig_mouse_set_button(TIG_MOUSE_BUTTON_RIGHT, down);
        } else {
            // Hold: highlight everything (Left Alt). Tap: next target.
            tig_gamepad_key(SDL_SCANCODE_LALT, down);
            if (down) {
                tig_gamepad_west_down_ms = SDL_GetTicks();
            } else if (SDL_GetTicks() - tig_gamepad_west_down_ms < 250
                && tig_gamepad_cycle_fn != NULL) {
                tig_gamepad_cycle_fn();
            }
        }
        break;
    case SDL_GAMEPAD_BUTTON_NORTH:
        if (down) {
            tig_gamepad_tap(SDL_SCANCODE_SPACE);
        }
        break;
    case SDL_GAMEPAD_BUTTON_START:
        if (down) {
            tig_gamepad_tap(SDL_SCANCODE_ESCAPE);
        }
        break;
    case SDL_GAMEPAD_BUTTON_BACK:
        if (down) {
            tig_gamepad_back_down_ms = SDL_GetTicks();
            tig_gamepad_back_fired = false;
        } else {
            if (tig_gamepad_back_down_ms != 0 && !tig_gamepad_back_fired) {
                tig_gamepad_tap(SDL_SCANCODE_F7);
            }
            tig_gamepad_back_down_ms = 0;
        }
        break;
    case SDL_GAMEPAD_BUTTON_GUIDE:
        tig_gamepad_guide_down = down;
        break;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        tig_gamepad_precision = down;
        break;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        if (down) {
            tig_gamepad_tap(SDL_SCANCODE_HOME);
        }
        break;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        if (down) {
            tig_gamepad_l1_down = true;
            tig_gamepad_l1_used = false;
        } else {
            if (tig_gamepad_l1_down && !tig_gamepad_l1_used && !tig_gamepad_hint_shown) {
                tig_gamepad_tap(SDL_SCANCODE_K);
            }
            tig_gamepad_l1_down = false;
        }
        break;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        if (down) {
            tig_gamepad_r1_down = true;
            tig_gamepad_r1_used = false;
        } else {
            if (tig_gamepad_r1_down && !tig_gamepad_r1_used && !tig_gamepad_hint_shown) {
                tig_gamepad_tap(SDL_SCANCODE_M);
            }
            tig_gamepad_r1_down = false;
        }
        break;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        if (!down) {
            break;
        }

        switch (button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            dpad = 0;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            dpad = 1;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            dpad = 2;
            break;
        default:
            dpad = 3;
            break;
        }

        if (tig_gamepad_l2_down) {
            layer = 1;
        } else if (tig_gamepad_l1_down) {
            layer = 2;
            tig_gamepad_l1_used = true;
        } else if (tig_gamepad_r1_down) {
            layer = 3;
            tig_gamepad_r1_used = true;
        } else {
            layer = 0;
            if (tig_gamepad_dpad_fn != NULL && tig_gamepad_dpad_fn(dpad)) {
                break;
            }
            // In windows and menus the d-pad moves between buttons.
            if (tig_gamepad_on_map_fn != NULL && !tig_gamepad_on_map_fn()) {
                tig_gamepad_focus_move(dpad);
                break;
            }
        }

        if (dpad_keys[layer][dpad] != SDL_SCANCODE_UNKNOWN) {
            tig_gamepad_tap(dpad_keys[layer][dpad]);
        }
        break;
    default:
        break;
    }
}

static void tig_gamepad_key(SDL_Scancode scancode, bool down)
{
    // key_event = true applies SDL's "latin_letters" keycode option, so the
    // game sees 'c', not Cyrillic 'с', with a Ukrainian layout active.
    tig_kb_set_key(SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, true), scancode, down);
}

static void tig_gamepad_tap(SDL_Scancode scancode)
{
    tig_debug_printf("Gamepad: tap %s\n", SDL_GetScancodeName(scancode));
    tig_gamepad_key(scancode, true);
    tig_gamepad_key(scancode, false);
}

static void tig_gamepad_move_cursor(float dx, float dy)
{
    TigMouseState mouse_state;
    int width;
    int height;
    int ix;
    int iy;
    int x;
    int y;

    tig_gamepad_cursor_rest_x += dx;
    tig_gamepad_cursor_rest_y += dy;
    ix = (int)tig_gamepad_cursor_rest_x;
    iy = (int)tig_gamepad_cursor_rest_y;
    if (ix == 0 && iy == 0) {
        return;
    }
    tig_gamepad_cursor_rest_x -= (float)ix;
    tig_gamepad_cursor_rest_y -= (float)iy;

    if (tig_mouse_get_state(&mouse_state) != TIG_OK
        || !tig_gamepad_screen_size(&width, &height)) {
        return;
    }

    x = SDL_clamp(mouse_state.x + ix, 0, width - 1);
    y = SDL_clamp(mouse_state.y + iy, 0, height - 1);
    if (x != mouse_state.x || y != mouse_state.y) {
        tig_mouse_set_position(x, y);
    }
}

static bool tig_gamepad_screen_size(int* width, int* height)
{
    SDL_Renderer* renderer;
    SDL_RendererLogicalPresentation mode;

    if (tig_video_renderer_get(&renderer) != TIG_OK) {
        return false;
    }

    if (!SDL_GetRenderLogicalPresentation(renderer, width, height, &mode)
        || *width <= 0
        || *height <= 0) {
        return SDL_GetCurrentRenderOutputSize(renderer, width, height);
    }

    return true;
}

static float tig_gamepad_axis(SDL_GamepadAxis axis)
{
    float value = 0.0f;
    int index;

    // Every connected pad contributes; the strongest input wins.
    for (index = 0; index < MAX_GAMEPADS; index++) {
        if (tig_gamepad_pads[index] != NULL) {
            float v = (float)SDL_GetGamepadAxis(tig_gamepad_pads[index], axis) / 32767.0f;
            if (fabsf(v) > fabsf(value)) {
                value = v;
            }
        }
    }

    return SDL_clamp(value, -1.0f, 1.0f);
}

static void tig_gamepad_stick(SDL_GamepadAxis axis_x, SDL_GamepadAxis axis_y, float* x, float* y)
{
    float sx = tig_gamepad_axis(axis_x);
    float sy = tig_gamepad_axis(axis_y);
    float mag = sqrtf(sx * sx + sy * sy);
    float scaled;

    // Radial dead zone, rescaled so movement starts smoothly at its edge.
    if (mag < STICK_DEADZONE) {
        *x = 0.0f;
        *y = 0.0f;
        return;
    }

    scaled = (SDL_min(mag, 1.0f) - STICK_DEADZONE) / (1.0f - STICK_DEADZONE);
    *x = sx / mag * scaled;
    *y = sy / mag * scaled;
}
