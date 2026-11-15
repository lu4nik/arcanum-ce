#ifndef BINK_FFMPEG_H_
#define BINK_FFMPEG_H_

// Local tweak: FFmpeg-backed implementation of the Bink calls the game uses.

#include "bink_compat.h"

typedef struct BinkFfmpegApi {
    void (*close)(HBINK);
    int (*copy_to_buffer)(HBINK, void*, int, unsigned, unsigned, unsigned, unsigned);
    int (*dd_surface_type)(void*);
    int (*do_frame)(HBINK);
    void (*next_frame)(HBINK);
    HBINK (*open)(const char*, unsigned);
    BINKSNDOPEN (*open_miles)(void*);
    int (*set_sound_system)(BINKSNDSYSOPEN, void*);
    void (*set_sound_track)(unsigned);
    int (*wait)(HBINK);
} BinkFfmpegApi;

void bink_ffmpeg_bind(BinkFfmpegApi* api);

#endif /* BINK_FFMPEG_H_ */
