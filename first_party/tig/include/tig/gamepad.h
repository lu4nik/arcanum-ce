#ifndef TIG_GAMEPAD_H_
#define TIG_GAMEPAD_H_

#include <SDL3/SDL.h>

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
