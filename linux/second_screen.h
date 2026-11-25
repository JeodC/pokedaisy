/* A second screen's window (SETTINGS > SECOND SCREEN): a dual-screen handheld's bottom screen, or a second monitor, with
 * the picture the core sends for it (PD_ENV_SECOND_SCREEN). POCKET_SECOND_SCREEN=WxH stands in for one, in a window. */
#ifndef POCKET_SECOND_SCREEN_H
#define POCKET_SECOND_SCREEN_H

#include <SDL.h>
#include <stdbool.h>

#include "pd_env.h"

#define SECOND_PICTURES 4
struct second_picture {
  const uint32_t *px; // ARGB8888
  int w, h;
  SDL_Rect box;    // in the page's pixels
  bool under;      // the page, else over it
  unsigned serial; // another for other pixels
};

SDL_Rect second_screen_fit(int w, int h, SDL_Rect in, bool cover);
bool second_screen_there(SDL_Window *window);
void second_screen_open(SDL_Window *window);
void second_screen_close(void);
bool second_screen_up(void);
void second_screen_swap(SDL_Window **window, SDL_Renderer **renderer);
bool second_screen_swapped(void);
bool second_screen_focused(void);
bool second_screen_event(const SDL_Event *e);
void second_screen_tap(int x, int y);
void second_screen_message(struct pd_second_screen *s, bool lcd);
bool second_screen_size(int *w, int *h);
void second_screen_page(const uint32_t *px, int w, int h, const struct second_picture *pics, int count, bool drawn);
void second_screen_blank(void);
void second_screen_present(void);
uint32_t *second_screen_pixels(int *w, int *h);

#endif
