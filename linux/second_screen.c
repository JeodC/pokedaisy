/* The second screen's window (see second_screen.h). */
#include "second_screen.h"

#include <stdio.h>
#include <stdlib.h>

#include "display_filter.h"

static SDL_Window *win;
static SDL_Renderer *out;
static struct layer {
  SDL_Texture *picture, *sharp;
  int w, h, k;
} under, over; // a DS's touch screen, the core's picture
static struct pd_second_screen last;
static struct pocket_lcd lcd; // over a DS's touch screen
static bool lcdOn, changed;
static int mouseX, mouseY, notches;
static bool held, pulse, pressed, beside;
static Uint32 inputAt;

static bool test_size(int *w, int *h) {
  const char *s = getenv("POCKET_SECOND_SCREEN");
  return s && sscanf(s, "%dx%d", w, h) == 2 && *w >= 160 && *h >= 144 && *w <= 8192 && *h <= 8192;
}

static int other_display(SDL_Window *window) {
  const int own = SDL_GetWindowDisplayIndex(window);
  for (int i = 0; i < SDL_GetNumVideoDisplays(); i++)
    if (i != own)
      return i;
  return -1;
}

bool second_screen_there(SDL_Window *window) {
  int w, h;
  return test_size(&w, &h) || other_display(window) >= 0;
}

void second_screen_open(SDL_Window *window) {
  int w = 0, h = 0;
  const bool test = test_size(&w, &h);
  const int display = test ? 0 : other_display(window);
  if (win || display < 0)
    return;
  SDL_Rect area = {0, 0, w, h};
  if (!test && SDL_GetDisplayBounds(display, &area))
    return;
  const bool full = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
  const int at = SDL_WINDOWPOS_CENTERED_DISPLAY(display);
  if (!test && !full)
    area.w = area.w * 3 / 4, area.h = area.h * 3 / 4;
  // ROCKNIX puts a window with "Secondary" in its title on the second screen, and turns that and its touch panel on.
  win = SDL_CreateWindow("PokeDaisy Secondary", at, at, area.w, area.h,
                         test ? 0 : full ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_RESIZABLE);
  // Its presents don't wait for a refresh: the game's window paces the host.
  out = win ? SDL_CreateRenderer(win, -1, 0) : NULL;
  if (!out) {
    fprintf(stderr, "Second screen: %s\n", SDL_GetError());
    second_screen_close();
    return;
  }
  if (!test)
    SDL_SetWindowMinimumSize(win, 320, 240);
  changed = true;
  SDL_RaiseWindow(window);
}

static void layer_drop(struct layer *l) {
  if (l->picture)
    SDL_DestroyTexture(l->picture);
  if (l->sharp)
    SDL_DestroyTexture(l->sharp);
  *l = (struct layer){0};
}

void second_screen_close(void) {
  layer_drop(&under);
  layer_drop(&over);
  pocket_lcd_drop(&lcd);
  if (out)
    SDL_DestroyRenderer(out);
  if (win)
    SDL_DestroyWindow(win);
  out = NULL;
  win = NULL;
  last.pixels = last.panel = NULL;
  held = pulse = pressed = beside = false;
}

bool second_screen_focused(void) { return win && SDL_GetKeyboardFocus() == win; }

bool second_screen_event(const SDL_Event *e) {
  const Uint32 id = win ? SDL_GetWindowID(win) : 0;
  if (!id)
    return false;
  switch (e->type) {
  case SDL_MOUSEBUTTONDOWN:
  case SDL_MOUSEBUTTONUP:
    if (e->button.windowID != id) {
      held = held && e->type != SDL_MOUSEBUTTONUP;
      return false;
    }
    mouseX = e->button.x, mouseY = e->button.y;
    if (e->button.button == SDL_BUTTON_LEFT) {
      held = e->type == SDL_MOUSEBUTTONDOWN;
      pulse |= held; // the core sees it even when the press and release fall in one frame
      inputAt = SDL_GetTicks();
    }
    return true;
  case SDL_MOUSEMOTION:
    if (e->motion.windowID != id)
      return false;
    mouseX = e->motion.x, mouseY = e->motion.y;
#ifndef _WIN32
    if (e->motion.which != SDL_TOUCH_MOUSEID)
      SDL_ShowCursor(SDL_ENABLE);
#endif
    return true;
  case SDL_MOUSEWHEEL:
    if (e->wheel.windowID != id)
      return false;
    notches += e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e->wheel.y : e->wheel.y;
    inputAt = SDL_GetTicks();
    return true;
  case SDL_WINDOWEVENT:
    if (e->window.windowID != id)
      return false;
    if (e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e->window.event == SDL_WINDOWEVENT_EXPOSED)
      changed = true;
    else if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST)
      held = false;
    return e->window.event != SDL_WINDOWEVENT_CLOSE; // the host turns the setting off
  default:
    return false;
  }
}

void second_screen_tap(int x, int y) {
  mouseX = x, mouseY = y;
  pulse = true;
  inputAt = SDL_GetTicks();
}

static SDL_Rect place(int srcW, int srcH) {
  int w = 0, h = 0;
  SDL_GetRendererOutputSize(out, &w, &h);
  SDL_Rect r = {0, 0, w, h};
  if (w * srcH > h * srcW)
    r.w = h * srcW / srcH;
  else
    r.h = w * srcH / srcW;
  r.x = (w - r.w) / 2;
  r.y = (h - r.h) / 2;
  return r;
}

void second_screen_message(struct pd_second_screen *s, bool lcdNow) {
  s->w = s->h = s->ptr_x = s->ptr_y = s->ptr_down = s->wheel = 0;
  if (!win)
    return;
  changed |= s->changed || s->pixels != last.pixels || s->panel != last.panel || lcdNow != lcdOn;
  last = *s;
  lcdOn = lcdNow;
  if (SDL_GetTicks() - inputAt > 250) // from while a menu or the game list was up
    pulse = false, notches = 0;
  SDL_GetRendererOutputSize(out, &s->w, &s->h);
  if (s->src_w <= 0 || s->src_h <= 0)
    return;
  const SDL_Rect r = place(s->src_w, s->src_h);
  if (r.w <= 0 || r.h <= 0)
    return;
  // A window's pixels aren't always its renderer's (a desktop's scale).
  int ww = 0, wh = 0;
  SDL_GetWindowSize(win, &ww, &wh);
  const int x = ww > 0 ? mouseX * s->w / ww : mouseX, y = wh > 0 ? mouseY * s->h / wh : mouseY;
  s->ptr_x = (x - r.x) * s->src_w / r.w;
  s->ptr_y = (y - r.y) * s->src_h / r.h;
  s->ptr_x = s->ptr_x < 0 ? 0 : s->ptr_x >= s->src_w ? s->src_w - 1 : s->ptr_x;
  s->ptr_y = s->ptr_y < 0 ? 0 : s->ptr_y >= s->src_h ? s->src_h - 1 : s->ptr_y;
  // A press that begins beside the picture is none.
  const bool down = held || pulse;
  if (down && !pressed)
    beside = x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h;
  pressed = down;
  s->ptr_down = down && !beside;
  s->wheel = notches;
  pulse = false;
  notches = 0;
}

void second_screen_blank(void) {
  last.pixels = last.panel = NULL;
  changed = true;
  second_screen_present();
}

static SDL_Texture *texture(int access, int w, int h, bool smooth) {
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, smooth ? "1" : "0");
  SDL_Texture *t = SDL_CreateTexture(out, SDL_PIXELFORMAT_ARGB8888, access, w, h);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  if (t)
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
  return t;
}

static void layer_draw(struct layer *l, const uint32_t *px, int w, int h, SDL_Rect r, SDL_BlendMode blend) {
  if (!l->picture || l->w != w || l->h != h) {
    layer_drop(l);
    l->picture = texture(SDL_TEXTUREACCESS_STREAMING, w, h, false);
    l->w = w;
    l->h = h;
  }
  if (!l->picture || SDL_UpdateTexture(l->picture, NULL, px, w * 4))
    return;
  const int k = r.w / w < r.h / h ? r.w / w : r.h / h;
  SDL_Texture *from = l->picture;
  // Between two whole scales: the one above first, then down to the screen, so its pixels stay even and nearly sharp.
  if (r.w != k * w || r.h != k * h) {
    if (!l->sharp || l->k != k + 1) {
      if (l->sharp)
        SDL_DestroyTexture(l->sharp);
      l->sharp = texture(SDL_TEXTUREACCESS_TARGET, w * (k + 1), h * (k + 1), true);
      l->k = k + 1;
    }
    if (l->sharp && !SDL_SetRenderTarget(out, l->sharp)) {
      SDL_SetTextureBlendMode(l->picture, SDL_BLENDMODE_NONE);
      SDL_RenderCopy(out, l->picture, NULL, NULL);
      SDL_SetRenderTarget(out, NULL);
      from = l->sharp;
    }
  }
  SDL_SetTextureBlendMode(from, blend);
  SDL_RenderCopy(out, from, NULL, &r);
}

static SDL_Rect within(SDL_Rect r, int srcW, int srcH, SDL_Rect box) {
  return (SDL_Rect){r.x + box.x * r.w / srcW, r.y + box.y * r.h / srcH, box.w * r.w / srcW, box.h * r.h / srcH};
}

static void draw(void) {
  SDL_SetRenderDrawColor(out, 0, 0, 0, 255);
  SDL_RenderClear(out);
  if (!last.pixels || last.src_w <= 0 || last.src_h <= 0)
    return;
  const SDL_Rect r = place(last.src_w, last.src_h);
  if (last.panel && last.panel_w > 0 && last.panel_h > 0) {
    const SDL_Rect box = within(r, last.src_w, last.src_h, (SDL_Rect){last.box_x, last.box_y, last.box_w, last.box_h});
    layer_draw(&under, last.panel, last.panel_w, last.panel_h, box, SDL_BLENDMODE_NONE);
    if (lcdOn)
      pocket_lcd_draw(&lcd, out, box);
  }
  layer_draw(&over, last.pixels, last.src_w, last.src_h, r, last.panel ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
}

void second_screen_present(void) {
  if (!win || !changed)
    return;
  changed = false;
  draw();
  SDL_RenderPresent(out);
}

uint32_t *second_screen_pixels(int *w, int *h) {
  if (!win || SDL_GetRendererOutputSize(out, w, h))
    return NULL;
  draw();
  uint32_t *px = malloc((size_t)*w * (size_t)*h * sizeof *px);
  if (px && SDL_RenderReadPixels(out, NULL, SDL_PIXELFORMAT_ARGB8888, px, *w * 4)) {
    free(px);
    px = NULL;
  }
  return px;
}
