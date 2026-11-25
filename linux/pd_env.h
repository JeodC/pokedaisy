/* Private libretro environment commands shared by PokeDaisy's cores and host. */
#ifndef PD_ENV_H
#define PD_ENV_H

#include <stdint.h>

/* The core's quick menu asks the frontend to turn fast-forward on or off. [data] is a bool. */
#define PD_ENV_SET_FAST_FORWARD 0x20001

/* The core's floating touch screen. The core keeps the game's bottom screen at its full 256 x 192 and asks a host that
 * can draw it to do the output scaling itself, instead of scaling the pixels down into the panel first. [data] is a
 * pd_touch_panel; the pointer stays valid until the core's next frame. active = 0 withdraws the panel. */
#define PD_ENV_SET_TOUCH_PANEL 0x20002
struct pd_touch_panel {
  const uint32_t *pixels; /* src_w x src_h, XRGB8888 (0xFFRRGGBB), as the frame's pixels are */
  int src_w, src_h;
  int x, y, w, h;         /* the screen's place in the core's canvas */
  int active;
};

/* How a core asks whether the frontend draws what PD_ENV_SET_HUD / PD_ENV_SET_COMPANION hand it: once at load, a struct
 * with active = PD_ENV_PROBE. A host that draws it sets active = 1 and returns true. Some frontends return true for every
 * private command and draw nothing (the likely reason a player's GKD 350H Ultra lost the HUD in 1.6.2: it came back on
 * 1.6.0's cores, which never asked), so the answer alone is not enough: a core
 * hands the picture over only when active came back 1, and keeps drawing it into its own canvas otherwise. */
#define PD_ENV_PROBE (-1)

/* The core's field HUD at its own resolution. A ZOOM step grows the core's canvas and the frontend scales it down to
 * the window, so a HUD blended into the picture is resampled with it: sharp at whole steps, soft at 1.25x and 1.5x. A
 * host that can draw it takes the HUD's own picture instead, drawn over its box in the window's pixels, so each HUD
 * pixel reaches the window from the picture once. [data] is a pd_hud; the pixels stay valid until the core's next
 * frame. active = 0 withdraws it. */
#define PD_ENV_SET_HUD 0x20003
struct pd_hud {
  const uint32_t *pixels; /* src_w x src_h, the HUD's own 256x192, ARGB8888, alpha 0 = clear */
  int src_w, src_h;
  int x, y, w, h;         /* the HUD's box in the core's canvas (the 1x top screen's place at every ZOOM step) */
  int pct;                /* 100: the picture over the whole box. 50: each quarter at its own corner, a quarter of the box */
  int active;
};

/* The companion as a window over the running game (the app's COMPANION VIEW > WINDOW on one screen): the core keeps the
 * game's canvas (dimmed), draws the companion into a picture of its own and asks the host to show it in the middle of the
 * window at a whole scale of the window's pixels (pd_companion_place). The host answers with the mouse over it, in the
 * picture's pixels, in the same struct. [data] is a pd_companion; the pixels stay valid until the core's next frame.
 * active = 0 withdraws it. A frontend without the command: the core keeps its companion filling the canvas. */
#define PD_ENV_SET_COMPANION 0x20004
struct pd_companion {
  const uint32_t *pixels; /* src_w x src_h, ARGB8888: alpha 0 past the window's rounded corners, else 0xFF */
  int src_w, src_h;
  int active;
  /* Filled by the host: the mouse in the picture's pixels; inside = over the picture; down = its button held;
   * valid = the host knows the mouse at all. */
  int ptr_x, ptr_y, ptr_inside, ptr_down, ptr_valid;
};

/* The host's own settings in the companion's SETTINGS, so a player has one place for all of them (the host's menu then
 * keeps RESUME / SAVE / LOAD / SETTINGS / QUIT). The core calls it every frame: the host fills the rows (A/B BUTTONS,
 * FF SPEED, ...) with their values, acts on [press] (a row's index, set by the core; -1 none) after the core's frame -
 * a value flips or steps, KEYBOARD / HOTKEYS open the host's own page over the game - and sets [open_settings] once
 * when its menu's SETTINGS was picked: the core then opens the companion on SETTINGS at these rows. A frontend without
 * the command: the companion shows no such rows. [data] is a pd_front_rows. */
#define PD_ENV_FRONT_ROWS 0x20005
#define PD_FRONT_ROWS_MAX 8
struct pd_front_rows {
  int press;                            /* in: the row the player pressed this frame, -1 none */
  int count;                            /* out: the rows */
  char label[PD_FRONT_ROWS_MAX][24];
  char value[PD_FRONT_ROWS_MAX][24];    /* "" = a row that opens a page */
  char group[PD_FRONT_ROWS_MAX][16];    /* the companion's SETTINGS group it goes in, as the app has it: "FF / SPEED",
                                           "GAMEPLAY", "SCREEN", "COMPANION", "CONTROLS", "APP" */
  int open_settings;                    /* out: 1 once, when the host's menu asked for the companion's SETTINGS */
};

/* A second screen (a dual-screen handheld's bottom one, a second monitor) for the companion or a DS's touch screen, with
 * the game on the first. The core calls it every frame with the picture it drew last, and the host answers with the screen
 * and the finger on it, in the same struct. A frontend without the command, or one that answers every private command
 * true, leaves w and h at 0: no second screen. [data] is a pd_second_screen; the pixels stay valid until the core's next
 * frame or its game's unload. With a [panel] (a DS's touch screen) the picture is ARGB8888 and goes over it, alpha 0 = clear.
 * The host fills the last three lines. */
#define PD_ENV_SECOND_SCREEN 0x20006
struct pd_second_screen {
  const uint32_t *pixels; /* src_w x src_h, XRGB8888; NULL = black */
  int src_w, src_h;
  int changed;            /* it isn't the picture of the call before */
  const uint32_t *panel;  /* panel_w x panel_h, XRGB8888, shown in the box; NULL = none */
  int panel_w, panel_h, box_x, box_y, box_w, box_h; /* the box is in the picture's pixels */
  int w, h;
  int ptr_x, ptr_y, ptr_down, wheel; /* the finger or mouse in the picture's pixels, the notches since the call before (+ up) */
  int above; /* it's over the game's screen, not under it */
};

/* Where the host puts the companion's picture in a ow x oh window: the largest whole scale that keeps it within 94% of the
 * window's height and 97% of its width (at least 1), centred. Shared, so the core sizes the picture (pd_companion_size)
 * for exactly the scale the host draws it at. Returns the scale. */
static inline int pd_companion_place(int ow, int oh, int sw, int sh, int *x, int *y, int *w, int *h) {
  int k = sw > 0 && sh > 0 ? (oh * 94 / 100) / sh : 0, kw = sw > 0 ? (ow * 97 / 100) / sw : 0;
  if (kw < k)
    k = kw;
  if (k < 1)
    k = 1;
  *w = sw * k;
  *h = sh * k;
  *x = (ow - *w) / 2;
  *y = (oh - *h) / 2;
  return k;
}

/* The companion window's padding: the companion is drawn this many pixels inside the window's picture on every side, its
 * backdrop running on under the frame (the app's window keeps its tabs clear of its frame the same way). */
#define PD_COMPANION_PAD 6

/* The companion window's picture for a ow x oh window: the companion inside it (PD_COMPANION_PAD round it) 240 to 270 game
 * pixels tall, the companion's own size on Android (a
 * font pixel about 1/240-1/270 of the screen's height; a taller picture drew every slot and row large around the same small
 * text), at the largest whole scale that keeps it within 94% of the window, 88% where that leaves more than 270 rows; as
 * wide as Android's window (0.78 of the window's aspect, between the Thor bottom screen's 1.15:1 and 1.4:1) and at least
 * 320. Returns that scale; 0 = the window can't show it at 2x (a small handheld screen: the companion fills it, as before).
 * [maxW] x [maxH]: the most the core holds. */
static inline int pd_companion_size(int ow, int oh, int maxW, int maxH, int *sw, int *sh) {
  if (ow <= 0 || oh <= 0)
    return 0;
  const int pad2 = 2 * PD_COMPANION_PAD;
  int k = (oh * 94 / 100) / (240 + pad2);
  if (k < 2)
    return 0;
  int h = (oh * 88 / 100) / k - pad2;
  if (h < 240)
    h = 240;
  if (h > 270)
    h = 270;
  int aspect1000 = (int)((long long)ow * 780 / oh); /* 0.78 of the window's aspect, in 1/1000ths */
  if (aspect1000 < 1148)
    aspect1000 = 1148;
  if (aspect1000 > 1400)
    aspect1000 = 1400;
  int w = h * aspect1000 / 1000;
  if (w > (ow * 97 / 100) / k - pad2)
    w = (ow * 97 / 100) / k - pad2;
  if (w < 320) /* the companion's layouts want 320: wider than Android's shape on a 4:3 window */
    w = 320;
  if ((w + pad2) * k > ow * 97 / 100)
    return 0;
  if (w + pad2 > maxW)
    w = maxW - pad2;
  if (h + pad2 > maxH)
    h = maxH - pad2;
  *sw = w + pad2;
  *sh = h + pad2;
  return k;
}

/* The companion's picture for a w x h second screen: at the largest whole scale that leaves it 320 x minH, or, where that
 * draws its text too small (48 rows over it: the Thor's 1240x1080), 320 wide or minH tall, scaled by the host. */
static inline void pd_second_size(int w, int h, int minH, int maxW, int maxH, int *sw, int *sh) {
  int k = w / 320 < h / minH ? w / 320 : h / minH;
  if (k < 1)
    k = 1;
  *sw = w / k;
  *sh = h / k;
  if (*sh > minH + 48) {
    const int wide = w * minH / h;
    *sw = wide > 320 ? wide : 320;
    *sh = wide > 320 ? minH : h * 320 / w;
  }
  if (*sw > maxW)
    *sw = maxW;
  if (*sh > maxH)
    *sh = maxH;
}

#endif
