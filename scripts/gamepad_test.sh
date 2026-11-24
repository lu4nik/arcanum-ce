#!/bin/sh
# Run the installed game with a gamepad script (see tig/src/gamepad_script.c)
# and wait until the script quits or TIMEOUT seconds pass.
# Usage: scripts/gamepad_test.sh SCRIPT [TIMEOUT]
SCRIPT="$1"
TIMEOUT="${2:-60}"
APP="/Applications/Arcanum/Arcanum Community Edition.app"
if pgrep -x arcanum-ce >/dev/null; then
    echo "game is running, not starting a test" >&2
    exit 1
fi
caffeinate -dims -t "$TIMEOUT" &
if [ -n "$OFFSCREEN" ]; then
    ARCANUM_GAMEPAD_SCRIPT="$SCRIPT" SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1 SDL_VIDEO_DRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIO_DRIVER=dummy "$APP/Contents/MacOS/arcanum-ce" >/dev/null 2>&1 &
else
    open -n --env ARCANUM_GAMEPAD_SCRIPT="$SCRIPT" --env SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1 "$APP"
fi
i=0
while [ $i -lt "$TIMEOUT" ]; do
    sleep 1
    i=$((i + 1))
    grep -q "Gamepad script: quit" "$HOME/Library/Logs/arcanum-ce.log" 2>/dev/null && break
done
sleep 1
pkill -9 -x arcanum-ce
exit 0
