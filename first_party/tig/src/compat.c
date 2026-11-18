#define COMPAT_NO_SDL_PATH_WRAPPERS
#include "tig/compat.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#else
#include <dirent.h>
#endif

#include <string.h>

// Local tweak: the game keeps text in cp1251 (the Ukrainian translation), while
// the file system and SDL text input speak UTF-8. These convert at the edges.
static const unsigned short compat_cp1251_high[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x0000, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
    0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
    0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
    0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F
};

static size_t compat_utf8_encode(unsigned int cp, char* out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
}

static bool compat_is_utf8(const char* str)
{
    const unsigned char* pch = (const unsigned char*)str;

    while (*pch != '\0') {
        int extra;

        if (*pch < 0x80) {
            pch++;
            continue;
        }

        if (*pch >= 0xC2 && *pch <= 0xDF) {
            extra = 1;
        } else if (*pch >= 0xE0 && *pch <= 0xEF) {
            extra = 2;
        } else if (*pch >= 0xF0 && *pch <= 0xF4) {
            extra = 3;
        } else {
            return false;
        }

        pch++;
        while (extra > 0) {
            if ((*pch & 0xC0) != 0x80) {
                return false;
            }
            pch++;
            extra--;
        }
    }

    return true;
}

void compat_cp1251_to_utf8(char* str, size_t size)
{
    char tmp[1024];
    size_t out = 0;
    const unsigned char* pch;
    bool high = false;

    for (pch = (const unsigned char*)str; *pch != '\0'; pch++) {
        if (*pch >= 0x80) {
            high = true;
            break;
        }
    }
    if (!high) {
        return;
    }

    // Paths may pass through here twice (public file functions call each
    // other with already converted paths). Real cp1251 text is practically
    // never valid UTF-8, so a valid UTF-8 string is taken as converted.
    if (compat_is_utf8(str)) {
        return;
    }

    for (pch = (const unsigned char*)str; *pch != '\0' && out + 4 < sizeof(tmp); pch++) {
        unsigned int cp = *pch < 0x80 ? *pch : compat_cp1251_high[*pch - 0x80];
        if (cp == 0) {
            cp = '_';
        }
        out += compat_utf8_encode(cp, &tmp[out]);
    }
    tmp[out] = '\0';

    if (out < size) {
        memcpy(str, tmp, out + 1);
    }
}

void compat_utf8_to_cp1251(char* str)
{
    const unsigned char* in = (const unsigned char*)str;
    char* out = str;

    while (*in != '\0') {
        unsigned int cp;
        int extra;

        if (*in < 0x80) {
            *out++ = (char)*in++;
            continue;
        }

        if ((*in & 0xE0) == 0xC0) {
            cp = *in & 0x1F;
            extra = 1;
        } else if ((*in & 0xF0) == 0xE0) {
            cp = *in & 0x0F;
            extra = 2;
        } else if ((*in & 0xF8) == 0xF0) {
            cp = *in & 0x07;
            extra = 3;
        } else {
            in++;
            continue;
        }
        in++;
        while (extra > 0 && (*in & 0xC0) == 0x80) {
            cp = (cp << 6) | (*in & 0x3F);
            in++;
            extra--;
        }
        if (extra != 0) {
            continue;
        }

        if (cp == 0x2019 || cp == 0x02BC) {
            // Typographic apostrophes from the Ukrainian layout.
            *out++ = '\'';
            continue;
        }

        for (int b = 0; b < 128; b++) {
            if (compat_cp1251_high[b] == cp) {
                *out++ = (char)(0x80 + b);
                break;
            }
        }
    }
    *out = '\0';
}

// Native paths are UTF-8 on every platform: that is what SDL takes and returns
// (on Windows too), and compat_fopen widens them for _wfopen there.
FILE* compat_fopen(const char* path, const char* mode)
{
#ifdef _WIN32
    wchar_t wpath[TIG_MAX_PATH];
    wchar_t wmode[16];

    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, TIG_MAX_PATH) == 0
        || MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 16) == 0) {
        return NULL;
    }

    return _wfopen(wpath, wmode);
#else
    return fopen(path, mode);
#endif
}

void compat_windows_path_to_native(char* path)
{
#ifdef _WIN32
    compat_cp1251_to_utf8(path, TIG_MAX_PATH);
#else
    char* pch = path;
    while (*pch != '\0') {
        if (*pch == '\\') {
            *pch = '/';
        }
        pch++;
    }
    compat_cp1251_to_utf8(path, TIG_MAX_PATH);
#endif
}

void compat_resolve_path(char* path)
{
#ifdef _WIN32
    (void)path;
#else
    char* pch = path;

    DIR* dir;
    if (pch[0] == '/') {
        dir = opendir("/");
        pch++;
    } else {
        dir = opendir(".");
    }

    while (dir != NULL) {
        char* sep = strchr(pch, '/');
        size_t length;
        if (sep != NULL) {
            length = sep - pch;
        } else {
            length = strlen(pch);
        }

        bool found = false;

        struct dirent* entry = readdir(dir);
        while (entry != NULL) {
            if (strlen(entry->d_name) == length && SDL_strncasecmp(pch, entry->d_name, length) == 0) {
                strncpy(pch, entry->d_name, length);
                found = true;
                break;
            }
            entry = readdir(dir);
        }

        closedir(dir);
        dir = NULL;

        if (!found) {
            break;
        }

        if (sep == NULL) {
            break;
        }

        *sep = '\0';
        dir = opendir(path);
        *sep = '/';

        pch = sep + 1;
    }
#endif
}

void compat_append_path(char* path, size_t size, const char* comp)
{
    size_t path_len = strlen(path);
    size_t comp_len = strlen(comp);

    if (path_len + comp_len + 1 >= size) {
        return;
    }

    if (path_len != 0) {
        path[path_len++] = PATH_SEPARATOR;
        path[path_len] = '\0';
    }

    strncat(path, comp, comp_len);
}

void compat_join_path_ex(char* path, size_t size, ...)
{
    va_list args;
    const char* comp;
    size_t comp_len;
    size_t path_len = 0;

    path[0] = '\0';

    va_start(args, size);

    while ((comp = va_arg(args, const char*)) != NULL) {
        comp_len = strlen(comp);
        if (path_len + comp_len + 1 >= size) {
            break;
        }

        if (path_len != 0) {
            path[path_len++] = PATH_SEPARATOR;
            path[path_len] = '\0';
        }

        strncat(path, comp, comp_len);
        path_len += comp_len;
    }

    va_end(args);
}

void compat_splitpath(const char* path, char* drive, char* dir, char* fname, char* ext)
{
#ifdef _WIN32
    _splitpath(path, drive, dir, fname, ext);
#else
    const char* drive_start = path;
    if (path[0] == '/' && path[1] == '/') {
        path += 2;
        while (*path != '\0' && *path != '/' && *path != '.') {
            path++;
        }
    }

    if (drive != NULL) {
        size_t drive_size = path - drive_start;
        if (drive_size > COMPAT_MAX_DRIVE - 1) {
            drive_size = COMPAT_MAX_DRIVE - 1;
        }
        strncpy(drive, path, drive_size);
        drive[drive_size] = '\0';
    }

    const char* dir_start = path;
    const char* fname_start = path;
    const char* ext_start = NULL;

    const char* end = path;
    while (*end != '\0') {
        if (*end == '/') {
            fname_start = end + 1;
        } else if (*end == '.') {
            ext_start = end;
        }
        end++;
    }

    if (ext_start == NULL) {
        ext_start = end;
    }

    if (dir != NULL) {
        size_t dir_size = fname_start - dir_start;
        if (dir_size > COMPAT_MAX_DIR - 1) {
            dir_size = COMPAT_MAX_DIR - 1;
        }
        strncpy(dir, path, dir_size);
        dir[dir_size] = '\0';
    }

    if (fname != NULL) {
        size_t fname_length = ext_start - fname_start;
        if (fname_length > COMPAT_MAX_FNAME - 1) {
            fname_length = COMPAT_MAX_FNAME - 1;
        }
        strncpy(fname, fname_start, fname_length);
        fname[fname_length] = '\0';
    }

    if (ext != NULL) {
        size_t ext_length = end - ext_start;
        if (ext_length > COMPAT_MAX_EXT - 1) {
            ext_length = COMPAT_MAX_EXT - 1;
        }
        strncpy(ext, ext_start, ext_length);
        ext[ext_length] = '\0';
    }
#endif
}

void compat_makepath(char* path, const char* drive, const char* dir, const char* fname, const char* ext)
{
#ifdef _WIN32
    _makepath(path, drive, dir, fname, ext);
#else
    path[0] = '\0';

    if (drive != NULL) {
        if (*drive != '\0') {
            strcpy(path, drive);
            path = strchr(path, '\0');

            if (path[-1] == '/') {
                path--;
            } else {
                *path = '/';
            }
        }
    }

    if (dir != NULL) {
        if (*dir != '\0') {
            if (*dir != '/' && *path == '/') {
                path++;
            }

            strcpy(path, dir);
            path = strchr(path, '\0');

            if (path[-1] == '/') {
                path--;
            } else {
                *path = '/';
            }
        }
    }

    if (fname != NULL && *fname != '\0') {
        if (*fname != '/' && *path == '/') {
            path++;
        }

        strcpy(path, fname);
        path = strchr(path, '\0');
    } else {
        if (*path == '/') {
            path++;
        }
    }

    if (ext != NULL) {
        if (*ext != '\0') {
            if (*ext != '.') {
                *path++ = '.';
            }

            strcpy(path, ext);
            path = strchr(path, '\0');
        }
    }

    *path = '\0';
#endif
}

#ifdef _WIN32
static void compat_sdl_path(char* dst, size_t size, const char* src)
{
    SDL_strlcpy(dst, src, size);
    compat_cp1251_to_utf8(dst, size);
}

bool compat_sdl_get_path_info(const char* path, SDL_PathInfo* info)
{
    char tmp[1024];
    compat_sdl_path(tmp, sizeof(tmp), path);
    return SDL_GetPathInfo(tmp, info);
}

bool compat_sdl_remove_path(const char* path)
{
    char tmp[1024];
    compat_sdl_path(tmp, sizeof(tmp), path);
    return SDL_RemovePath(tmp);
}

bool compat_sdl_rename_path(const char* oldpath, const char* newpath)
{
    char tmp1[1024];
    char tmp2[1024];
    compat_sdl_path(tmp1, sizeof(tmp1), oldpath);
    compat_sdl_path(tmp2, sizeof(tmp2), newpath);
    return SDL_RenamePath(tmp1, tmp2);
}

bool compat_sdl_create_directory(const char* path)
{
    char tmp[1024];
    compat_sdl_path(tmp, sizeof(tmp), path);
    return SDL_CreateDirectory(tmp);
}

char** compat_sdl_glob_directory(const char* path, const char* pattern, SDL_GlobFlags flags, int* count)
{
    char tmp1[1024];
    char tmp2[1024];
    compat_sdl_path(tmp1, sizeof(tmp1), path);
    compat_sdl_path(tmp2, sizeof(tmp2), pattern);
    return SDL_GlobDirectory(tmp1, tmp2, flags, count);
}
#endif
