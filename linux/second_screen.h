/* A second screen's window (SETTINGS > SECOND SCREEN): a dual-screen handheld's bottom screen, or a second monitor, with
 * the picture the core sends for it (PD_ENV_SECOND_SCREEN). POCKET_SECOND_SCREEN=WxH stands in for one, in a window. */
#ifndef POCKET_SECOND_SCREEN_H
#define POCKET_SECOND_SCREEN_H

#include <SDL.h>
#include <stdbool.h>

#include "pd_env.h"

bool second_screen_there(SDL_Window *window);
void second_screen_open(SDL_Window *window);
void second_screen_close(void);
bool second_screen_focused(void);
bool second_screen_event(const SDL_Event *e);
void second_screen_tap(int x, int y);
void second_screen_message(struct pd_second_screen *s, bool lcd);
void second_screen_blank(void);
void second_screen_present(void);
uint32_t *second_screen_pixels(int *w, int *h);

#endif
