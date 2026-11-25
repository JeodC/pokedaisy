/* The LCD mask over a renderer's output (see display_filter.h). */
#include "display_filter.h"

#include <stdlib.h>

void pocket_lcd_draw(struct pocket_lcd *lcd, SDL_Renderer *r, SDL_Rect box) {
  const int w = box.w, h = box.h;
  if (w <= 0 || h <= 0)
    return;
  if (!lcd->mask || lcd->w != w || lcd->h != h) {
    pocket_lcd_drop(lcd);
    uint32_t *mask = malloc((size_t)w * h * 4);
    if (!mask)
      return;
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        mask[y * w + x] = pocket_lcd_pixel(x, y);
    lcd->mask = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (lcd->mask && (SDL_UpdateTexture(lcd->mask, NULL, mask, w * 4) < 0 ||
                      SDL_SetTextureBlendMode(lcd->mask, SDL_BLENDMODE_MOD) < 0))
      pocket_lcd_drop(lcd);
    free(mask);
    lcd->w = w;
    lcd->h = h;
  }
  if (lcd->mask)
    SDL_RenderCopy(r, lcd->mask, NULL, &box);
}

void pocket_lcd_drop(struct pocket_lcd *lcd) {
  if (lcd->mask)
    SDL_DestroyTexture(lcd->mask);
  lcd->mask = NULL;
}
