/* The game list's second screen (see library_media.h). */
#include "library_media.h"

#include <SDL.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cover_png.h"
#include "pd_style.h"
#include "pd_tr.h"

#define CLEAR 0xFF01FE // the canvas's key: the backdrop's picture shows there, darkened
#define PAD 8
#define GAP 6
#define ROW_H 18
#define LINE_H 16
#define SETTLE_MS 100 // a game the cursor only passes loads nothing
#define STORY_PAUSE_MS 3000
#define STORY_MS_PER_PX 40
#define MAX_SIDE 960

struct art {
  uint32_t *px[ART_COUNT];
  int w[ART_COUNT], h[ART_COUNT];
};

static SDL_Thread *loader;
static SDL_mutex *lock;
static SDL_cond *wake;
// Under the lock: the game whose pictures are wanted, and those read since the UI thread last took them.
static struct {
  int id;
  char path[ART_COUNT][PATH_MAX];
} want;
static struct art fresh;
static int freshId;
static bool freshAll, stop;
// The UI thread's: the cursor's game's so far, and the last game's whose all came. Its backdrop stays till then.
static struct art part, full;
static int partId, fullId, cursorId, askedId;
static unsigned serial;
static Uint32 cursorAt;
static uint64_t drawnKey, wrapKey;
static int storyLines;
static struct second_picture pictures[SECOND_PICTURES]; // the backdrop, the box, the logo and the screenshot
static int pictureCount;

static void art_free(struct art *a) {
  for (int i = 0; i < ART_COUNT; i++)
    free(a->px[i]);
  *a = (struct art){0};
}

static void art_put(struct art *a, int i, uint32_t *px, int w, int h) {
  free(a->px[i]);
  a->px[i] = px;
  a->w[i] = w;
  a->h[i] = h;
}

static int load(void *unused) {
  (void)unused;
  static char path[ART_COUNT][PATH_MAX];
  int done = -1;
  SDL_LockMutex(lock);
  while (!stop) {
    if (want.id == done || want.id < 0) {
      SDL_CondWait(wake, lock);
      continue;
    }
    const int id = want.id;
    memcpy(path, want.path, sizeof path);
    for (int i = ART_COUNT - 1; i >= 0 && want.id == id && !stop; i--) { // the box first, the backdrop last
      int w = 0, h = 0;
      SDL_UnlockMutex(lock);
      uint32_t *px = path[i][0] ? cover_png_load(path[i], MAX_SIDE, &w, &h) : NULL;
      SDL_LockMutex(lock);
      if (freshId != id) {
        art_free(&fresh);
        freshId = id;
      }
      art_put(&fresh, i, px, w, h);
      freshAll = i == 0;
    }
    if (freshAll && freshId == id)
      done = id;
  }
  SDL_UnlockMutex(lock);
  return 0;
}

void media_open(void) {
  if (!lock) {
    lock = SDL_CreateMutex();
    wake = SDL_CreateCond();
  }
  want.id = freshId = partId = fullId = cursorId = askedId = -1;
  freshAll = false;
  drawnKey = wrapKey = 0;
  stop = false;
  loader = lock && wake ? SDL_CreateThread(load, "media", NULL) : NULL;
}

void media_close(void) {
  if (loader) {
    SDL_LockMutex(lock);
    stop = true;
    SDL_CondSignal(wake);
    SDL_UnlockMutex(lock);
    SDL_WaitThread(loader, NULL);
    loader = NULL;
  }
  art_free(&fresh);
  art_free(&part);
  art_free(&full);
  pictureCount = 0;
}

void media_free(struct media *m) {
  for (int i = 0; i < ART_BOX; i++)
    free(m->art[i]);
  for (int i = 0; i < FACT_COUNT; i++)
    free(m->fact[i]);
  free(m->story);
  *m = (struct media){0};
}

static const char *art_path(const struct media_game *g, int i) { return i == ART_BOX ? g->cover : g->media->art[i]; }

// Shown already, or still to come.
static bool art_has(const struct media_game *g, int i) {
  const char *path = g->id >= 0 ? art_path(g, i) : NULL;
  return path && path[0] && loader && (fullId != g->id || full.px[i]);
}

static void follow(const struct media_game *g) {
  if (g->id != cursorId) {
    cursorId = g->id;
    cursorAt = SDL_GetTicks();
  }
  if (!loader)
    return;
  SDL_LockMutex(lock);
  if (freshId >= 0 && freshId != partId) {
    art_free(&part);
    partId = freshId;
    serial++;
  }
  for (int i = 0; freshId >= 0 && i < ART_COUNT; i++)
    if (fresh.px[i]) {
      art_put(&part, i, fresh.px[i], fresh.w[i], fresh.h[i]);
      fresh.px[i] = NULL;
      serial++;
    }
  if (freshId >= 0 && freshAll) {
    art_free(&full);
    full = part;
    fullId = partId;
    part = (struct art){0};
    partId = freshId = -1;
    freshAll = false;
    serial++;
  }
  if (g->id >= 0 && g->id != askedId && SDL_GetTicks() - cursorAt >= SETTLE_MS) {
    for (int i = 0; i < ART_COUNT; i++) {
      const char *path = art_path(g, i);
      snprintf(want.path[i], sizeof want.path[i], "%s", path ? path : "");
    }
    want.id = askedId = g->id;
    SDL_CondSignal(wake);
  }
  SDL_UnlockMutex(lock);
}

static SDL_Rect fit(const struct art *a, int i, SDL_Rect in) { return second_screen_fit(a->w[i], a->h[i], in, false); }

static void picture(const struct art *a, int i, SDL_Rect box, bool under) {
  pictures[pictureCount++] = (struct second_picture){a->px[i], a->w[i], a->h[i], box, under, serial};
}

static SDL_Rect framed(struct pd_canvas *c, SDL_Rect r) {
  const struct pd_layer frame[2] = {{pd_cur->frameDark, 1}, {pd_cur->frameLight, 1}};
  pd_layered_box(c, r.x, r.y, r.w, r.h, 3, frame, 2, pd_cur->imageWell);
  return (SDL_Rect){r.x + 2, r.y + 2, r.w - 4, r.h - 4};
}

static void draw_logo(struct pd_canvas *c, const struct media_game *g, const struct art *a, SDL_Rect in) {
  if (a && a->px[ART_LOGO]) {
    picture(a, ART_LOGO, fit(a, ART_LOGO, in), false);
    return;
  }
  if (art_has(g, ART_LOGO))
    return;
  int w = pd_text_width(g->title) + 32, h = 30;
  w = w > in.w ? in.w : w;
  const int x = in.x + (in.w - w) / 2, y = in.y + (in.h - h) / 2, f = pd_title_box(c, x, y, w, h, PD_TITLE_FILL);
  pd_text_fit(c, x + f + 8, pd_text_y(y, h), w - 2 * f - 16, g->title, PD_TITLE_TEXT, PD_TITLE_SHADOW);
}

static void draw_box(struct pd_canvas *c, const struct media_game *g, const struct art *a, SDL_Rect in) {
  if (a && a->px[ART_BOX]) {
    picture(a, ART_BOX, framed(c, fit(a, ART_BOX, in)), false);
    return;
  }
  if (art_has(g, ART_BOX))
    return;
  const int side = (in.w < in.h ? in.w : in.h) * 7 / 10;
  const SDL_Rect well = framed(c, (SDL_Rect){in.x + (in.w - side) / 2, in.y + (in.h - side) / 2, side, side});
  pd_clip(c, well.x, well.y, well.w, well.h);
  g->cartridge(c, g->id, well.x, well.y, well.w);
  pd_unclip(c);
}

// In a short box LAST PLAYED and SAVE are the first to go.
static void draw_facts(struct pd_canvas *c, const struct media_game *g, SDL_Rect in) {
  static const char *const LABELS[FACT_COUNT] = {"RELEASED", "DEVELOPER", "PUBLISHER", "GENRE", "PLAYERS", "RATING"};
  const char *label[FACT_COUNT + 2], *value[FACT_COUNT + 2];
  int n = 0;
  for (int i = 0; i < FACT_COUNT; i++)
    if (g->media->fact[i])
      label[n] = LABELS[i], value[n++] = g->media->fact[i];
  label[n] = "LAST PLAYED", value[n++] = g->played ? g->played : pd_tr("NEVER");
  label[n] = "SAVE", value[n++] = pd_tr(g->save ? "YES" : "NONE");
  const int f = pd_list_box(c, in.x, in.y, in.w, in.h), room = (in.h - 2 * f) / ROW_H;
  n = n > room ? room : n;
  const int x = in.x + f + 6, w = in.w - 2 * f - 12, split = w * 46 / 100;
  for (int i = 0, y = in.y + (in.h - n * ROW_H) / 2; i < n; i++, y += ROW_H) {
    pd_text_fit(c, x, pd_text_y(y, ROW_H), split - 4, pd_tr(label[i]), PD_MUTED, PD_MUTED_SHADOW);
    pd_text_fit(c, x + split, pd_text_y(y, ROW_H), w - split, value[i], PD_VALUE, PD_VALUE_SHADOW);
  }
}

// In canvas pixels: down slowly and back, with a pause at each end.
static int story_scroll(int overflow) {
  if (overflow <= 0)
    return 0;
  const Uint32 way = (Uint32)overflow * STORY_MS_PER_PX, cycle = 2 * (STORY_PAUSE_MS + way);
  Uint32 t = (SDL_GetTicks() - cursorAt) % cycle;
  if (t < STORY_PAUSE_MS)
    return 0;
  t -= STORY_PAUSE_MS;
  if (t < way)
    return (int)(t / STORY_MS_PER_PX);
  t -= way;
  return t < STORY_PAUSE_MS ? overflow : overflow - (int)((t - STORY_PAUSE_MS) / STORY_MS_PER_PX);
}

struct places {
  SDL_Rect logo, box, facts, screen, story;
};
// The app's two layouts: a wide screen has the box art down its left, a squarer one the logo on top.
static struct places place(const struct pd_canvas *c, bool shot, bool story) {
  const int w = c->w - 2 * PAD, h = c->h - 2 * PAD;
  struct places p = {0};
  if (c->w * 100 >= c->h * 145) {
    const int boxW = (w - GAP) * 4 / 10, x = PAD + boxW + GAP, rest = w - boxW - GAP, logoH = h / 5;
    const int rowH = (h - logoH - 2 * GAP) * (story ? 55 : 100) / 100, rowY = PAD + logoH + GAP;
    p.box = (SDL_Rect){PAD, PAD, boxW, h};
    p.logo = (SDL_Rect){x, PAD, rest, logoH};
    p.screen = (SDL_Rect){x, rowY, shot ? (rest - GAP) / 2 : 0, rowH};
    p.facts = (SDL_Rect){x + (shot ? p.screen.w + GAP : 0), rowY, rest - (shot ? p.screen.w + GAP : 0), rowH};
    p.story = (SDL_Rect){x, rowY + rowH + GAP, rest, PAD + h - rowY - rowH - GAP};
    return p;
  }
  const int logoH = h * 17 / 100, rowY = PAD + logoH + GAP, room = h - logoH - 2 * GAP;
  const int rowH = shot || story ? room * 56 / 100 : room + GAP, boxW = (w - GAP) * 42 / 100, lowY = rowY + rowH + GAP, lowH = room - rowH;
  p.logo = (SDL_Rect){PAD, PAD, w, logoH};
  p.box = (SDL_Rect){PAD, rowY, boxW, rowH};
  p.facts = (SDL_Rect){PAD + boxW + GAP, rowY, w - boxW - GAP, rowH};
  const int half = (w - GAP) / 2, shotW = !shot ? 0 : !story ? w : lowH * 3 / 2 < half ? lowH * 3 / 2 : half;
  p.screen = (SDL_Rect){PAD, lowY, shotW, lowH};
  p.story = (SDL_Rect){PAD + (shot ? shotW + GAP : 0), lowY, w - (shot ? shotW + GAP : 0), lowH};
  return p;
}

// What the backdrop's picture keeps of its light, from the top down: the app's 60%, 30% and 10%.
static uint32_t shade(int y, int h) {
  const int half = h / 2 ? h / 2 : 1;
  return (uint32_t)(y < half ? 0x66 + (0xB3 - 0x66) * y / half : 0xB3 + (0xE6 - 0xB3) * (y - half) / (h - half));
}

// False = nothing changed since the last page.
bool media_draw(struct pd_canvas *c, const struct media_game *g, const struct second_picture **pics, int *count) {
  follow(g);
  const struct art *a = g->id < 0 ? NULL : fullId == g->id ? &full : partId == g->id ? &part : NULL;
  const char *story = g->id >= 0 ? g->media->story : NULL;
  const struct places p = place(c, art_has(g, ART_SCREEN), story != NULL);
  const int storyW = p.story.w - 28;
  const uint64_t wrap = ((uint64_t)(g->id + 2) << 32 | (uint32_t)storyW << 8) + (uint64_t)pd_tr_language();
  if (wrap != wrapKey) {
    wrapKey = wrap;
    storyLines = story ? pd_text_wrap(NULL, 0, 0, storyW, LINE_H, 200, story, 0, 0) : 0;
  }
  const int scroll = story_scroll(storyLines * LINE_H + 12 - p.story.h);
  *pics = pictures;
  *count = pictureCount;
  const uint64_t key = (wrap ^ (uint64_t)serial << 44 ^ (uint64_t)scroll << 16 ^ (uint64_t)(c->w * 4099 + c->h)) * 31 +
                       (uint64_t)(uintptr_t)pd_cur + (g->save ? 1 : 0) + (pd_dark ? 2 : 0);
  if (key == drawnKey)
    return false;
  drawnKey = key;
  pictureCount = 0;
  const int back = g->id < 0 ? -1 : full.px[ART_FANART] ? ART_FANART : full.px[ART_SCREEN] ? ART_SCREEN : -1;
  pd_unclip(c);
  pd_fill(c, 0, 0, c->w, c->h, back >= 0 ? CLEAR : pd_dark ? 0x12141C : 0xF7F6FB);
  if (back >= 0)
    picture(&full, back, second_screen_fit(full.w[back], full.h[back], (SDL_Rect){0, 0, c->w, c->h}, true), true);
  if (g->id >= 0) {
    draw_logo(c, g, a, p.logo);
    draw_box(c, g, a, p.box);
    draw_facts(c, g, p.facts);
    if (a && a->px[ART_SCREEN])
      picture(a, ART_SCREEN, framed(c, fit(a, ART_SCREEN, p.screen)), false);
    if (story) {
      const int f = pd_list_box(c, p.story.x, p.story.y, p.story.w, p.story.h);
      pd_clip(c, p.story.x + f, p.story.y + f, p.story.w - 2 * f, p.story.h - 2 * f);
      pd_text_wrap(c, p.story.x + 14, p.story.y + 6 - scroll, storyW, LINE_H, 200, story, PD_LABEL, PD_LABEL_SHADOW);
      pd_unclip(c);
    }
  }
  uint32_t *px = c->px;
  for (int y = 0; y < c->h; y++) {
    const uint32_t clear = shade(y, c->h) << 24;
    for (int x = 0; x < c->w; x++, px++)
      *px = (*px & 0xFFFFFF) == CLEAR ? clear : 0xFF000000u | *px;
  }
  *count = pictureCount;
  return true;
}
