#ifndef TIG_GAMEPAD_SCRIPT_H_
#define TIG_GAMEPAD_SCRIPT_H_

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

// Local tweak: plays ARCANUM_GAMEPAD_SCRIPT through a virtual gamepad
// (testing aid, see gamepad_script.c). Called once per tig ping.
void tig_gamepad_script_ping(void);

#ifdef __cplusplus
}
#endif

#endif /* TIG_GAMEPAD_SCRIPT_H_ */
