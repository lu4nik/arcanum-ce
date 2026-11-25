#include "game/highres_config.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

static void highres_config_reset(void);
static void highres_config_parse_line(char* line);
static void highres_config_trim(char** start_ptr, char** end_ptr);
static bool highres_config_parse_int(const char* str, int* value_ptr);

static HighResConfig highres_config;

void highres_config_load(void)
{
    FILE* stream;
    char line[512];

    highres_config_reset();

    stream = fopen("HighRes/config.ini", "rt");
    if (stream == NULL) {
        return;
    }

    highres_config.loaded = true;

    while (fgets(line, sizeof(line), stream) != NULL) {
        highres_config_parse_line(line);
    }

    fclose(stream);
}

const HighResConfig* highres_config_get(void)
{
    return &highres_config;
}

void highres_config_reset(void)
{
    highres_config.loaded = false;
    highres_config.width = 0;
    highres_config.height = 0;
    highres_config.scale = 0;
    highres_config.windowed = false;
    highres_config.show_fps = false;
    highres_config.smooth = -1;
    highres_config.smooth_ui = true;
    highres_config.renderer = 0;
    highres_config.wheel_scroll = 24;
    highres_config.auto_save = 10;
    highres_config.gamepad = true;
    highres_config.gamepad_cursor_speed = 900;
    highres_config.gamepad_pan_speed = 1100;
    highres_config.scroll_limit = true;
    highres_config.scroll_fps = 35;
    highres_config.scroll_dist = 10;
    highres_config.logos = true;
    highres_config.intro = true;
}

void highres_config_parse_line(char* line)
{
    char* comment;
    char* sep;
    char* key_start;
    char* key_end;
    char* value_start;
    char* value_end;
    int value;

    comment = strstr(line, "//");
    if (comment != NULL) {
        *comment = '\0';
    }

    key_start = line;
    key_end = key_start + strlen(key_start);
    highres_config_trim(&key_start, &key_end);
    *key_end = '\0';

    if (*key_start == '\0') {
        return;
    }

    sep = strchr(key_start, '=');
    if (sep == NULL) {
        return;
    }

    *sep = '\0';

    key_end = sep;
    highres_config_trim(&key_start, &key_end);
    *key_end = '\0';

    value_start = sep + 1;
    value_end = value_start + strlen(value_start);
    highres_config_trim(&value_start, &value_end);
    *value_end = '\0';

    if (!highres_config_parse_int(value_start, &value)) {
        return;
    }

    if (SDL_strcasecmp(key_start, "Width") == 0) {
        if (value >= 800 || value == 0) {
            highres_config.width = value;
        }
    } else if (SDL_strcasecmp(key_start, "Height") == 0) {
        if (value >= 600 || value == 0) {
            highres_config.height = value;
        }
    } else if (SDL_strcasecmp(key_start, "Scale") == 0) {
        highres_config.scale = value >= 100 && value <= 800 ? value : 0;
    } else if (SDL_strcasecmp(key_start, "Windowed") == 0) {
        highres_config.windowed = value != 0;
    } else if (SDL_strcasecmp(key_start, "ShowFPS") == 0) {
        highres_config.show_fps = value != 0;
    } else if (SDL_strcasecmp(key_start, "ScrollFPS") == 0) {
        if (value > 0) {
            highres_config.scroll_fps = value;
        }
    } else if (SDL_strcasecmp(key_start, "ScrollDist") == 0) {
        if (value >= 0) {
            highres_config.scroll_dist = value;
        }
    } else if (SDL_strcasecmp(key_start, "Logos") == 0) {
        highres_config.logos = value != 0;
    } else if (SDL_strcasecmp(key_start, "Intro") == 0) {
        highres_config.intro = value != 0;
    } else if (SDL_strcasecmp(key_start, "Smooth") == 0) {
        highres_config.smooth = value >= -1 && value <= 3 ? value : -1;
    } else if (SDL_strcasecmp(key_start, "WheelScroll") == 0) {
        highres_config.wheel_scroll = value;
    } else if (SDL_strcasecmp(key_start, "Renderer") == 0) {
        highres_config.renderer = value == 1 ? 1 : 0;
    } else if (SDL_strcasecmp(key_start, "AutoSave") == 0) {
        highres_config.auto_save = value >= 0 ? value : 0;
    } else if (SDL_strcasecmp(key_start, "SmoothUI") == 0) {
        highres_config.smooth_ui = value != 0;
    } else if (SDL_strcasecmp(key_start, "Gamepad") == 0) {
        highres_config.gamepad = value != 0;
    } else if (SDL_strcasecmp(key_start, "GamepadCursorSpeed") == 0) {
        highres_config.gamepad_cursor_speed = value > 0 ? value : 900;
    } else if (SDL_strcasecmp(key_start, "GamepadPanSpeed") == 0) {
        highres_config.gamepad_pan_speed = value > 0 ? value : 1100;
    } else if (SDL_strcasecmp(key_start, "ScrollLimit") == 0) {
        highres_config.scroll_limit = value != 0;
    }
}

void highres_config_trim(char** start_ptr, char** end_ptr)
{
    while (*start_ptr < *end_ptr && isspace((unsigned char)**start_ptr)) {
        (*start_ptr)++;
    }

    while (*end_ptr > *start_ptr && isspace((unsigned char)*((*end_ptr) - 1))) {
        (*end_ptr)--;
    }
}

bool highres_config_parse_int(const char* str, int* value_ptr)
{
    char* end;
    long value;

    if (*str == '\0') {
        return false;
    }

    value = strtol(str, &end, 10);
    if (end == str) {
        return false;
    }

    while (*end != '\0') {
        if (!isspace((unsigned char)*end)) {
            return false;
        }

        end++;
    }

    *value_ptr = (int)value;
    return true;
}

bool highres_config_save_int(const char* key, int value)
{
    const char* path = "HighRes/config.ini";
    char* text = NULL;
    size_t size = 0;
    FILE* stream;
    char line[512];
    char out_line[128];
    bool replaced = false;
    size_t key_len = strlen(key);
    char* out;
    size_t out_size;
    size_t out_len = 0;

    snprintf(out_line, sizeof(out_line), "%s=%d", key, value);

    stream = fopen(path, "rt");
    if (stream != NULL) {
        fseek(stream, 0, SEEK_END);
        size = (size_t)ftell(stream);
        fseek(stream, 0, SEEK_SET);
        text = (char*)SDL_malloc(size + 1);
        if (text == NULL) {
            fclose(stream);
            return false;
        }
        size = fread(text, 1, size, stream);
        text[size] = '\0';
        fclose(stream);
    } else {
        SDL_CreateDirectory("HighRes");
    }

    out_size = size + sizeof(out_line) + 4;
    out = (char*)SDL_malloc(out_size);
    if (out == NULL) {
        SDL_free(text);
        return false;
    }
    out[0] = '\0';

    if (text != NULL) {
        char* cursor = text;
        while (*cursor != '\0') {
            char* eol = strchr(cursor, '\n');
            size_t len = eol != NULL ? (size_t)(eol - cursor + 1) : strlen(cursor);
            const char* p = cursor;

            while (p < cursor + len && (*p == ' ' || *p == '\t')) {
                p++;
            }

            if (!replaced
                && SDL_strncasecmp(p, key, key_len) == 0
                && (p[key_len] == '=' || p[key_len] == ' ' || p[key_len] == '\t')) {
                bool crlf = len >= 2 && cursor[len - 2] == '\r';
                out_len += (size_t)snprintf(out + out_len, out_size - out_len, "%s%s", out_line, crlf ? "\r\n" : (eol != NULL ? "\n" : ""));
                replaced = true;
            } else {
                memcpy(out + out_len, cursor, len);
                out_len += len;
                out[out_len] = '\0';
            }

            cursor += len;
        }
    }

    if (!replaced) {
        if (out_len > 0 && out[out_len - 1] != '\n') {
            out[out_len++] = '\n';
        }
        out_len += (size_t)snprintf(out + out_len, out_size - out_len, "%s\n", out_line);
    }

    (void)line;
    stream = fopen(path, "wb");
    if (stream == NULL) {
        SDL_free(out);
        SDL_free(text);
        return false;
    }
    fwrite(out, 1, out_len, stream);
    fclose(stream);

    SDL_free(out);
    SDL_free(text);

    snprintf(line, sizeof(line), "%s=%d", key, value);
    highres_config_parse_line(line);
    return true;
}
