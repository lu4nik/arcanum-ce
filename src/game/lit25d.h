#ifndef ARCANUM_GAME_LIT25D_H_
#define ARCANUM_GAME_LIT25D_H_

// Local experiment: game side of the 2.5D lighting pass (tig/lit25d.h).
// Keeps the G-buffer in sync with the iso window and feeds the lights.

#include "game/context.h"

bool lit25d_init(tig_window_handle_t iso_window_handle);
void lit25d_exit(void);
void lit25d_resize(tig_window_handle_t iso_window_handle);
bool lit25d_active(void);
void lit25d_cycle_mode(void);
void lit25d_clear(const TigRect* rect);
void lit25d_scroll(int dx, int dy);
void lit25d_blit(const TigArtBlitInfo* blit_info, int anchor_y, int cls);
bool lit25d_margin_pass(void);
void lit25d_emit(const TigArtBlitInfo* blit_info);
void lit25d_draw_margin(GameDrawInfo* draw_info);

#endif /* ARCANUM_GAME_LIT25D_H_ */
