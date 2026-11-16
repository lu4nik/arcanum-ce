#ifndef GAME_HIGHRES_CONFIG_H_
#define GAME_HIGHRES_CONFIG_H_

#include <stdbool.h>

typedef struct HighResConfig {
    bool loaded;
    int width;
    int height;
    bool windowed;
    bool show_fps;
    int scroll_fps;
    int scroll_dist;
    bool logos;
    bool intro;
    int smooth; // Local tweak: upscale filter 0 off, 1 xBR, 2 xBRZ, 3 ScaleFX (GPU)
    bool smooth_ui; // Local tweak: also smooth UI windows and text
    int renderer; // Local tweak: 0 OpenGL (default), 1 Metal
    int wheel_scroll; // Local tweak: map pan per wheel unit in pixels, 0 = off, <0 = inverted
    int auto_save; // Local tweak: autosave every N real minutes into two rotating slots, 0 = off

} HighResConfig;

void highres_config_load(void);
const HighResConfig* highres_config_get(void);

#endif /* GAME_HIGHRES_CONFIG_H_ */
