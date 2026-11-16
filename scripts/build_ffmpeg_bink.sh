#!/bin/sh
# Fork tweak: build the minimal static FFmpeg (LGPL-2.1) used to play the
# game's Bink movies on macOS: only the Bink demuxer and decoders, nothing else.
# Installs into out/ffmpeg-bink (the default FFMPEG_BINK_PREFIX).
set -e
VER=7.1
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK="$ROOT/out/ffmpeg-src"
PREFIX="$ROOT/out/ffmpeg-bink"
mkdir -p "$WORK"
cd "$WORK"
[ -f ffmpeg-$VER.tar.xz ] || curl -L -o ffmpeg-$VER.tar.xz https://ffmpeg.org/releases/ffmpeg-$VER.tar.xz
[ -d ffmpeg-$VER ] || tar -xf ffmpeg-$VER.tar.xz
cd ffmpeg-$VER
./configure --prefix="$PREFIX" --cc=clang --enable-static --disable-shared \
    --disable-autodetect --disable-everything --disable-programs --disable-doc \
    --disable-network --disable-avdevice --disable-swscale --disable-swresample \
    --disable-postproc --disable-avfilter --enable-protocol=file \
    --enable-demuxer=bink --enable-decoder=bink --enable-decoder=binkaudio_rdft \
    --enable-decoder=binkaudio_dct --extra-cflags="-mmacosx-version-min=12.0"
make -j"$(sysctl -n hw.ncpu)"
make install
echo "FFmpeg (Bink only) installed in $PREFIX"
