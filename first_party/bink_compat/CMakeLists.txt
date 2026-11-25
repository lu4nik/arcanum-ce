add_library(bink_compat STATIC
    "include/bink_compat.h"
    "src/bink_compat.c"
)

target_include_directories(bink_compat PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

set(BINK_COMPAT_LIBRARY "bink_compat" PARENT_SCOPE)
set(BINK_COMPAT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include" PARENT_SCOPE)

# Local tweak: on macOS decode Bink with a minimal static FFmpeg build
# (Bink demuxer + decoders only). Point FFMPEG_BINK_PREFIX at its install dir.
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(FFMPEG_BINK_PREFIX "${CMAKE_SOURCE_DIR}/out/ffmpeg-bink" CACHE PATH "Minimal FFmpeg (Bink only) install prefix, see scripts/build_ffmpeg_bink.sh")
    if(EXISTS "${FFMPEG_BINK_PREFIX}/lib/libavformat.a")
        target_sources(bink_compat PRIVATE "src/bink_ffmpeg.c" "src/bink_ffmpeg.h")
        target_compile_definitions(bink_compat PRIVATE BINK_COMPAT_FFMPEG)
        target_include_directories(bink_compat PRIVATE "${FFMPEG_BINK_PREFIX}/include")
        target_link_libraries(bink_compat PUBLIC
            "${FFMPEG_BINK_PREFIX}/lib/libavformat.a"
            "${FFMPEG_BINK_PREFIX}/lib/libavcodec.a"
            "${FFMPEG_BINK_PREFIX}/lib/libavutil.a"
            SDL3::SDL3
        )
    else()
        message(WARNING "FFmpeg for Bink not found in ${FFMPEG_BINK_PREFIX}: movies will not play")
    endif()
endif()
