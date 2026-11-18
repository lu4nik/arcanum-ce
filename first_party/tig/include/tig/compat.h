#ifndef TIG_COMPAT_H_
#define TIG_COMPAT_H_

#include "tig/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMPAT_MAX_DRIVE 3
#define COMPAT_MAX_DIR 256
#define COMPAT_MAX_FNAME 256
#define COMPAT_MAX_EXT 256

#ifdef _WIN32
#define PATH_SEPARATOR '\\'
#define PATH_SEPARATOR_STR "\\"
#else
#define PATH_SEPARATOR '/'
#define PATH_SEPARATOR_STR "/"
#endif

void compat_windows_path_to_native(char* path);
void compat_cp1251_to_utf8(char* str, size_t size);
void compat_utf8_to_cp1251(char* str);

#ifdef _WIN32
// Local tweak (Windows): the game passes paths in cp1251 (system ANSI code page,
// what fopen expects), but SDL file functions take UTF-8. Convert at the edge.
#ifndef COMPAT_NO_SDL_PATH_WRAPPERS
#define SDL_GetPathInfo compat_sdl_get_path_info
#define SDL_RemovePath compat_sdl_remove_path
#define SDL_RenamePath compat_sdl_rename_path
#define SDL_CreateDirectory compat_sdl_create_directory
#define SDL_GlobDirectory compat_sdl_glob_directory
#endif
bool compat_sdl_get_path_info(const char* path, SDL_PathInfo* info);
bool compat_sdl_remove_path(const char* path);
bool compat_sdl_rename_path(const char* oldpath, const char* newpath);
bool compat_sdl_create_directory(const char* path);
char** compat_sdl_glob_directory(const char* path, const char* pattern, SDL_GlobFlags flags, int* count);
#endif
void compat_resolve_path(char* path);
void compat_append_path(char* path, size_t size, const char* comp);
void compat_join_path_ex(char* path, size_t size, ...);
#define compat_join_path(path, size, ...) compat_join_path_ex(path, size, __VA_ARGS__, NULL)
void compat_splitpath(const char* path, char* drive, char* dir, char* fname, char* ext);
void compat_makepath(char* path, const char* drive, const char* dir, const char* fname, const char* ext);

#ifdef __cplusplus
}
#endif

#endif /* TIG_COMPAT_H_ */
