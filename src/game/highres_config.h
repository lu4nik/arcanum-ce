#ifndef GAME_HIGHRES_CONFIG_H_
#define GAME_HIGHRES_CONFIG_H_

#include <stdbool.h>

typedef struct HighResConfig {
    bool loaded;
    int width; // Local tweak: 0 = automatic (with height 0)
    int height;
    int scale; // Local tweak: forced scale in percent for automatic size, 0 = auto
    bool windowed;
    bool show_fps;
    int scroll_fps;
    int scroll_dist;
    bool logos;
    bool intro;
    int smooth; // Local tweak: upscale filter 0 off, 1 xBR, 2 xBRZ, 3 ScaleFX (GPU), -1 = best available
    bool smooth_ui; // Local tweak: also smooth UI windows and text
    int renderer; // Local tweak: 0 OpenGL (default), 1 Metal
    int wheel_scroll; // Local tweak: map pan per wheel unit in pixels, 0 = off, <0 = inverted
    int auto_save; // Local tweak: autosave every N real minutes into two rotating slots, 0 = off
    bool gamepad; // Local tweak: gamepad as virtual mouse/keyboard
    int gamepad_cursor_speed; // Local tweak: cursor px/s at full stick
    int gamepad_pan_speed; // Local tweak: map pan px/s at full right stick
    bool scroll_limit; // Local tweak: stick/trackpad scrolling keeps the character on screen

} HighResConfig;

void highres_config_load(void);
const HighResConfig* highres_config_get(void);

// Local tweak: write one integer key back to HighRes/config.ini (replacing
// the existing line or appending one) and update the loaded value.
bool highres_config_save_int(const char* key, int value);

#endif /* GAME_HIGHRES_CONFIG_H_ */
