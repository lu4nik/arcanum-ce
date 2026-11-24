#include "tig/gamepad_script.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tig/debug.h"
#include "tig/kb.h"
#include "tig/mouse.h"
#include "tig/video.h"

// Local tweak: scripted virtual gamepad for testing gamepad support without
// a person holding the controller.
//
// ARCANUM_GAMEPAD_SCRIPT=/path/to/script attaches a virtual SDL gamepad and
// plays the script, one command per line ('#' starts a comment):
//
//   wait MS                  do nothing for MS milliseconds
//   press BUTTON [MS]        press and release (held MS ms, default 100)
//   hold BUTTON / release BUTTON
//   axis AXIS VALUE          stick/trigger, VALUE in -1..1 (triggers 0..1)
//   key SCANCODE             tap a keyboard key (SDL scancode name)
//   mouse X Y                put the cursor at X Y (game coordinates)
//   shot PATH                save the next frame as a BMP
//   state                    log cursor position
//   quit                     exit the game
//
// BUTTON and AXIS use SDL's gamepad names: a b x y back guide start
// leftstick rightstick leftshoulder rightshoulder dpup dpdown dpleft
// dpright touchpad; leftx lefty rightx righty lefttrigger righttrigger.

#define MAX_LINES 2048

static char** script_lines;
static int script_count;
static int script_pos;
static Uint64 script_resume_ms;
static SDL_Joystick* script_joystick;
static bool script_started;
static int script_release_button = -1;
static Uint64 script_release_ms;

static char* trim(char* s)
{
    char* end;

    while (*s == ' ' || *s == '\t') {
        s++;
    }
    end = s + strlen(s);
    while (end > s && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')) {
        *--end = '\0';
    }
    return s;
}

static bool script_load(const char* path)
{
    FILE* stream;
    char buf[512];

    stream = fopen(path, "r");
    if (stream == NULL) {
        tig_debug_printf("Gamepad script: cannot open %s\n", path);
        return false;
    }

    script_lines = (char**)calloc(MAX_LINES, sizeof(*script_lines));
    while (script_count < MAX_LINES && fgets(buf, sizeof(buf), stream) != NULL) {
        char* line = trim(buf);
        char* hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
            line = trim(line);
        }
        if (*line != '\0') {
            script_lines[script_count++] = strdup(line);
        }
    }
    fclose(stream);
    return true;
}

static bool script_attach(void)
{
    SDL_VirtualJoystickDesc desc;
    SDL_JoystickID id;

    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "Arcanum script gamepad";

    id = SDL_AttachVirtualJoystick(&desc);
    if (id == 0) {
        tig_debug_printf("Gamepad script: cannot attach: %s\n", SDL_GetError());
        return false;
    }

    script_joystick = SDL_OpenJoystick(id);
    return script_joystick != NULL;
}

static void script_button(int button, bool down)
{
    SDL_SetJoystickVirtualButton(script_joystick, button, down);
}

static void script_exec(const char* line)
{
    char cmd[32] = { 0 };
    char a[256] = { 0 };
    char b[64] = { 0 };
    Uint64 now = SDL_GetTicks();

    sscanf(line, "%31s %255s %63s", cmd, a, b);
    tig_debug_printf("Gamepad script: %s\n", line);

    if (strcmp(cmd, "wait") == 0) {
        script_resume_ms = now + (Uint64)atoi(a);
    } else if (strcmp(cmd, "press") == 0 || strcmp(cmd, "hold") == 0 || strcmp(cmd, "release") == 0) {
        SDL_GamepadButton button = SDL_GetGamepadButtonFromString(a);
        if (button == SDL_GAMEPAD_BUTTON_INVALID) {
            tig_debug_printf("Gamepad script: unknown button %s\n", a);
            return;
        }
        if (strcmp(cmd, "release") == 0) {
            script_button(button, false);
        } else {
            script_button(button, true);
            if (strcmp(cmd, "press") == 0) {
                int ms = b[0] != '\0' ? atoi(b) : 100;
                script_release_button = button;
                script_release_ms = now + (Uint64)ms;
                // Let the release happen and settle before the next line.
                script_resume_ms = now + (Uint64)ms + 60;
            }
        }
    } else if (strcmp(cmd, "axis") == 0) {
        SDL_GamepadAxis axis = SDL_GetGamepadAxisFromString(a);
        float value = (float)atof(b);
        if (axis == SDL_GAMEPAD_AXIS_INVALID) {
            tig_debug_printf("Gamepad script: unknown axis %s\n", a);
            return;
        }
        SDL_SetJoystickVirtualAxis(script_joystick, axis, (Sint16)SDL_clamp(value * 32767.0f, -32768.0f, 32767.0f));
    } else if (strcmp(cmd, "key") == 0) {
        SDL_Scancode scancode = SDL_GetScancodeFromName(a);
        tig_kb_set_key(SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, true), scancode, true);
        tig_kb_set_key(SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, true), scancode, false);
        script_resume_ms = now + 60;
    } else if (strcmp(cmd, "mouse") == 0) {
        tig_mouse_set_position(atoi(a), atoi(b));
    } else if (strcmp(cmd, "shot") == 0) {
        tig_video_capture_next_frame(a);
        script_resume_ms = now + 100;
    } else if (strcmp(cmd, "state") == 0) {
        TigMouseState mouse_state;
        if (tig_mouse_get_state(&mouse_state) == TIG_OK) {
            tig_debug_printf("Gamepad script: cursor %d %d\n", mouse_state.x, mouse_state.y);
        }
    } else if (strcmp(cmd, "quit") == 0) {
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&event);
    } else {
        tig_debug_printf("Gamepad script: unknown command %s\n", cmd);
    }
}

void tig_gamepad_script_ping(void)
{
    Uint64 now;

    if (!script_started) {
        const char* path = SDL_getenv("ARCANUM_GAMEPAD_SCRIPT");
        script_started = true;
        if (path == NULL || !script_load(path) || !script_attach()) {
            script_count = 0;
            return;
        }
        tig_debug_printf("Gamepad script: %d lines from %s\n", script_count, path);
        // Give the game a moment to notice the new gamepad.
        script_resume_ms = SDL_GetTicks() + 500;
    }

    if (script_joystick == NULL) {
        return;
    }

    now = SDL_GetTicks();

    if (script_release_button != -1 && now >= script_release_ms) {
        script_button(script_release_button, false);
        script_release_button = -1;
    }

    while (script_pos < script_count && now >= script_resume_ms && script_release_button == -1) {
        script_exec(script_lines[script_pos++]);
        if (script_resume_ms > now || script_release_button != -1) {
            break;
        }
    }
}
