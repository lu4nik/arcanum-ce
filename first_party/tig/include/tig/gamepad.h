#ifndef TIG_GAMEPAD_H_
#define TIG_GAMEPAD_H_

#include <SDL3/SDL.h>

#include "tig/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Local tweak: play with a gamepad (DualSense, Xbox, ...) as a virtual mouse
// and keyboard. The game itself is untouched: sticks, touchpad and buttons are
// turned into the same cursor moves, clicks and key presses a mouse and
// keyboard would produce.

void tig_gamepad_set_enabled(bool enabled);
void tig_gamepad_set_cursor_speed(int pixels_per_second);
void tig_gamepad_set_pan_speed(int pixels_per_second);

// Returns true while the cursor is over something worth aiming at (a map
// object). The stick cursor briefly stops when it reaches such a target and
// moves slower over it, so it does not overshoot while the stick is let go.
void tig_gamepad_set_sticky_callback(bool (*fn)(void));

// Returns true while the player is on the map (not in a window or menu).
// Off the map circle is "back" (Esc) and square is the right click.
void tig_gamepad_set_on_map_callback(bool (*fn)(void));

// Called for a d-pad press without modifiers (0 up, 1 right, 2 down,
// 3 left); returns true if the game used it (e.g. to pick a dialogue reply).
void tig_gamepad_set_dpad_callback(bool (*fn)(int direction));

// Extra focus targets for screens whose clickable things are not tig
// buttons (inventory slots, list rows...). Fills rects (x/y = a point to put
// the cursor on, width/height = size) and returns how many.
void tig_gamepad_set_targets_callback(int (*fn)(TigRect* rects, int max));

// Called for every SDL event; returns true if the event was a gamepad event.
bool tig_gamepad_handle_event(const SDL_Event* event);

// Called once per tig ping: integrates stick positions into cursor movement
// and map panning, handles held-button timers.
void tig_gamepad_ping(void);

// Map panning requested with the right stick since the last call, in pixels.
void tig_gamepad_pan_take(float* dx, float* dy);

#ifdef __cplusplus
}
#endif

#endif /* TIG_GAMEPAD_H_ */
