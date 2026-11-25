/* The second screen's page for the game under the game list's cursor (the app's LauncherMediaPane): its box art, logo and
 * screenshot, its facts and its story, from the firmware's gamelist.xml. The pictures are decoded off the UI thread. */
#ifndef POCKET_LIBRARY_MEDIA_H
#define POCKET_LIBRARY_MEDIA_H

#include <stdbool.h>

#include "pd_canvas.h"
#include "second_screen.h"

enum { ART_FANART, ART_LOGO, ART_SCREEN, ART_BOX, ART_COUNT };
enum { FACT_RELEASED, FACT_DEVELOPER, FACT_PUBLISHER, FACT_GENRE, FACT_PLAYERS, FACT_RATING, FACT_COUNT };

struct media {
  char *art[ART_BOX], *fact[FACT_COUNT], *story; // the box art is the cover
};

struct media_game {
  int id; // -1 = no game
  const char *title, *cover, *played;
  bool save;
  const struct media *media;
  void (*cartridge)(struct pd_canvas *c, int id, int x, int y, int s);
};

void media_open(void);
void media_close(void);
void media_free(struct media *m);
bool media_draw(struct pd_canvas *c, const struct media_game *g, const struct second_picture **pics, int *count);

#endif
