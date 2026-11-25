/* shared_save.c: which file is a game's, and a Gambatte clock there and back. */
#include "shared_save.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char dir[] = "/tmp/pd-shared-XXXXXX";
static char got[4096];

static const char *in(const char *name) {
  static char p[4][4096];
  static int i;
  char *out = p[i++ & 3];
  snprintf(out, sizeof p[0], "%s/%s", dir, name);
  return out;
}
static void put(const char *name, const void *data, size_t n) {
  FILE *f = fopen(in(name), "wb");
  assert(f && fwrite(data, 1, n, f) == n && !fclose(f));
}
static int find(const char *rom, const char *name) { return shared_save_path(in(rom), name, got, sizeof got); }
static bool is(const char *rom, const char *name, const char *want) {
  return find(rom, name) == 1 && !strcmp(got, in(want));
}
static bool none(const char *rom, const char *name) { return !find(rom, name); }
static bool bad(const char *rom, const char *name) { return find(rom, name) == -1; }

static void paths(void) {
  static const char big[0x22001];
  assert(none("roms/gba/A.gba", "A.gba"));
  char roms[8192], saves[8192];
  snprintf(roms, sizeof roms, "%s/roms/gb:%s/roms/gba/:%s/roms/nds:%s/roms/off", dir, dir, dir, dir);
  snprintf(saves, sizeof saves, "%s/saves/gb:%s/saves/gba:%s/roms/nds:", dir, dir, dir);
  setenv("POCKET_ROM_DIRS", roms, 1);
  setenv("POCKET_SAVE_DIRS", saves, 1);
  assert(none("roms/gba/A.gba", "A.gba"));
  shared_saves_use(true);
  assert(is("roms/gba/A.gba", "A.gba", "saves/gba/A.srm") && is("roms/gb/A.gb", "A.gb", "saves/gb/A.srm"));
  assert(is("roms/gba//A.gba", "A.gba", "saves/gba/A.srm"));
  assert(is("roms/gba/Pack (USA).zip", "Pack (USA).gba", "saves/gba/Pack (USA).srm"));
  put("saves/gb/G.sav", big, 0x20000);
  assert(is("roms/gb/G.gb", "G.gb", "saves/gb/G.srm"));
  put("saves/gba/E.srm", "", 0);
  assert(bad("roms/gba/E.gba", "E.gba"));
  assert(is("roms/nds/A.nds", "A.nds", "roms/nds/A.sav") && is("roms/gba/a.b.gba", "a.b.gba", "saves/gba/a.b.srm"));
  assert(none("roms/off/A.gba", "A.gba") && none("roms/gba/sub/A.gba", "A.gba") && none("A.gba", "A.gba"));
  assert(!shared_save_path("A.gba", "A.gba", got, sizeof got) && none("roms/gba/A", "A") && none("roms/gba/.gba", ".gba"));
  assert(shared_save_path(in("roms/gba/A.gba"), "A.gba", got, 10) == -1);
  // The .srm before the .sav, which a DS game's size makes another game's.
  put("saves/gba/A.sav", big, sizeof big);
  assert(is("roms/gba/A.gba", "A.gba", "saves/gba/A.srm"));
  put("saves/gba/A.sav", "x", 1);
  assert(is("roms/gba/A.gba", "A.gba", "saves/gba/A.sav"));
  put("saves/gba/A.srm", "x", 1);
  assert(is("roms/gba/A.gba", "A.gba", "saves/gba/A.srm"));
  // Two ROMs of one name.
  put("roms/gb/T.gb", "x", 1);
  put("roms/gb/T.gbc", "x", 1);
  assert(none("roms/gb/T.gb", "T.gb"));
  put("saves/gb/T.srm", "x", 1);
  assert(bad("roms/gb/T.gb", "T.gb"));
  // RetroArch's compressed saves, and a link to a card that isn't there.
  put("saves/gba/Z.srm", "#RZIPv1#", 8);
  mkdir(in("saves/gba/D.srm"), 0755);
  assert(bad("roms/gba/Z.gba", "Z.gba") && bad("roms/gba/D.gba", "D.gba"));
  assert(!symlink(in("roms/real.srm"), in("saves/gba/L.srm")) && bad("roms/gba/L.gba", "L.gba"));
  put("roms/real.srm", "x", 1);
  assert(is("roms/gba/L.gba", "L.gba", "saves/gba/L.srm"));
  // A Game Boy game never takes a GBA game's bigger .srm, which it would write over.
  put("saves/gb/H.srm", big, 0x20000);
  assert(none("roms/gb/H.gb", "H.gb"));
  // VBA Next's GBA .srm, and one past it.
  put("saves/gba/V.srm", big, 0x22000);
  assert(is("roms/gba/V.gba", "V.gba", "saves/gba/V.srm"));
  put("saves/gba/V.srm", big, sizeof big);
  assert(none("roms/gba/V.gba", "V.gba"));
  // Two consoles' games of one name whose saves go to one folder: neither shares (PokeDaisy's own save instead).
  put("roms/gb/F.gb", "x", 1);
  put("roms/gba/F.gba", "x", 1);
  assert(is("roms/gb/F.gb", "F.gb", "saves/gb/F.srm") && is("roms/gba/F.gba", "F.gba", "saves/gba/F.srm"));
  snprintf(saves, sizeof saves, "%s/saves/gb:%s/saves/gb", dir, dir);
  setenv("POCKET_SAVE_DIRS", saves, 1);
  assert(none("roms/gb/F.gb", "F.gb") && none("roms/gba/F.gba", "F.gba"));
  assert(is("roms/gb/A.gb", "A.gb", "saves/gb/A.srm") && is("roms/gba/A.gba", "A.gba", "saves/gb/A.srm"));
  setenv("POCKET_SAVE_DIRS", "", 1);
  assert(none("roms/gba/A.gba", "A.gba"));
  setenv("POCKET_SAVE_DIRS", in("saves/gb"), 1);
  assert(is("roms/gb/A.gb", "A.gb", "saves/gb/A.srm") && none("roms/gba/A.gba", "A.gba"));
  setenv("POCKET_SAVE_DIRS", in("saves/missing"), 1);
  assert(bad("roms/gb/A.gb", "A.gb"));
}

static bool config(const char *text) {
  put("home/.config/retroarch/retroarch.cfg", text, strlen(text));
  shared_saves_use(true);
  return shared_saves_find();
}
#define UNSORTED "sort_savefiles_enable = \"false\"\n"
static void retroarch(void) {
  char text[4096 + 128];
  unsetenv("POCKET_SAVE_DIRS");
  unsetenv("XDG_CONFIG_HOME");
  setenv("HOME", in("home"), 1);
  assert(!shared_saves_find() && bad("roms/gba/N.gba", "N.gba") && is("roms/nds/N.nds", "N.nds", "roms/nds/N.sav"));
  // RetroArch's folder is never made.
  assert(!config(UNSORTED "savefile_directory = \"default\"\n") && !config(UNSORTED));
  assert(!config(UNSORTED "savefile_directory = \"~/kept\"\n") && access(in("home/kept"), F_OK));
  mkdir(in("home/kept"), 0755);
  mkdir(in("home/.config/retroarch/saves"), 0755);
  assert(config(UNSORTED) && is("roms/gba/N.gba", "N.gba", "home/.config/retroarch/saves/N.srm"));
  // The first of two counts, and the game's folder is added.
  assert(config("  savefile_directory = \"~/kept\"\r\nsort_savefiles_enable = \"false\"\r\n"
                "sort_savefiles_by_content_enable = \"true\"\r\nsavefile_directory = \"/no\"\r\n"));
  assert(is("roms/gba/N.gba", "N.gba", "home/kept/gba/N.srm") && is("roms/nds/N.nds", "N.nds", "roms/nds/N.sav"));
  assert(access(in("home/kept/gba"), F_OK) && !mkdir(in("home/kept/GBA"), 0755));
  assert(is("roms/gba/N.gba", "N.gba", "home/kept/GBA/N.srm") && is("roms/gba/F.gba", "F.gba", "home/kept/GBA/F.srm"));
  snprintf(text, sizeof text, "savefile_directory = %s/saves\n" UNSORTED, dir);
  assert(config(text) && is("roms/gba/N.gba", "N.gba", "saves/N.srm"));
  // RetroArch's one folder for every console: gb/F.gb and gba/F.gba would both be F.srm, an archive could hold either.
  assert(none("roms/gba/F.gba", "F.gba") && none("roms/gb/F.gb", "F.gb"));
  put("roms/off/O.gba", "x", 1); // the same console's: the same game
  assert(is("roms/gba/O.gba", "O.gba", "saves/O.srm"));
  put("roms/gb/K.zip", "x", 1);
  assert(none("roms/gba/K.gba", "K.gba") && is("roms/gba/Q.gba", "Q.gba", "saves/Q.srm"));
  assert(config("savefiles_in_content_dir = \"true\"\nsavefile_directory = \"/no\"\n" UNSORTED));
  assert(is("roms/gba/N.gba", "N.gba", "roms/gba/N.srm"));
  assert(config("savefiles_in_content_dir = true\nsort_savefiles_by_content_enable = true\n" UNSORTED));
  assert(is("roms/gba/N.gba", "N.gba", "roms/gba/gba/N.srm"));
  // Folders that can't be known, and compressed saves.
  assert(!config("savefile_directory = \"~/kept\"\n") && bad("roms/gba/N.gba", "N.gba"));
  assert(!config("sort_savefiles_enable = \"true\"\nsavefile_directory = \"~/kept\"\n"));
  assert(!config(UNSORTED "savefile_directory = \":/saves\"\n"));
  assert(!config(UNSORTED "save_file_compression = \"true\"\n"));
  shared_saves_use(false);
  assert(none("roms/gba/N.gba", "N.gba"));
}

static void store(unsigned char *p, uint64_t v, int n) {
  for (int i = 0; i < n; i++)
    p[i] = (unsigned char)(v >> (8 * i));
}
static uint64_t load(const unsigned char *p, int n) {
  uint64_t v = 0;
  for (int i = n; i--;)
    v = v << 8 | p[i];
  return v;
}
static void base(int64_t zero) {
  unsigned char b[8];
  store(b, (uint64_t)zero, 8);
  put("c.rtc", b, 8);
}

static void clocks(void) {
  char sav[4096], path[4096];
  unsigned char rtc[48], out[48];
  snprintf(sav, sizeof sav, "%s", in("c.srm"));
  memset(rtc, 7, sizeof rtc);
  assert(!shared_clock_load(sav, rtc, 48, 1000) && rtc[0] == 7 && !shared_clock_image(sav, rtc, 48, out, path, sizeof path));
  // 300 days, 5:06:07 since it read 0.
  const int64_t zero = 1740770297, now = zero + 300 * 86400LL + 5 * 3600 + 6 * 60 + 7;
  base(zero);
  assert(shared_clock_load(sav, rtc, 48, now));
  const uint32_t want[5] = {7, 6, 5, 300 & 255, 1};
  for (int i = 0; i < 5; i++)
    assert(load(rtc + 4 * i, 4) == want[i] && load(rtc + 20 + 4 * i, 4) == want[i]);
  assert(load(rtc + 40, 8) == (uint64_t)now);
  assert(!shared_clock_image(sav, rtc, 48, out, path, sizeof path));
  store(rtc + 20 + 8, 9, 4); // 9:06:07
  assert(shared_clock_image(sav, rtc, 48, out, path, sizeof path) == 8 && !strcmp(path, in("c.rtc")));
  assert((int64_t)load(out, 8) == zero - 4 * 3600);
  store(rtc + 20 + 16, 0x41, 4); // stopped
  assert(!shared_clock_image(sav, rtc, 48, out, path, sizeof path));
  // Past 511 days the clock wraps, and Gambatte moves the time it read 0 on by as much.
  assert(shared_clock_load(sav, rtc, 48, zero + 633 * 86400LL) && load(rtc + 12, 4) == 122 && load(rtc + 16, 4) == 0x80);
  assert(shared_clock_image(sav, rtc, 48, out, path, sizeof path) == 8 && (int64_t)load(out, 8) == zero + 511 * 86400LL);
  base(zero + 511 * 86400LL);
  assert(shared_clock_load(sav, rtc, 48, zero + 633 * 86400LL) && load(rtc + 12, 4) == 122 && !load(rtc + 16, 4));
  assert(!shared_clock_image(sav, rtc, 48, out, path, sizeof path));
  // A device whose date is behind the file's leaves it.
  base(zero);
  assert(shared_clock_load(sav, rtc, 48, zero - 5) && !load(rtc, 4) && !load(rtc + 16, 4));
  assert(!shared_clock_image(sav, rtc, 48, out, path, sizeof path));
  // mGBA's layout, and one of neither's size.
  memset(out, 9, sizeof out);
  put("c.rtc", out, 48);
  assert(shared_clock_load(sav, rtc, 48, now) && !memcmp(rtc, out, 48));
  memset(rtc, 3, sizeof rtc);
  assert(shared_clock_image(sav, rtc, 48, out, path, sizeof path) == 48 && out[47] == 3);
  put("c.rtc", out, 16);
  assert(!shared_clock_load(sav, rtc, 48, now) && !shared_clock_image(sav, rtc, 48, out, path, sizeof path));
  base(zero);
  assert(!shared_clock_load(sav, rtc, 44, now));
  base(-5);
  assert(!shared_clock_load(sav, rtc, 48, now) && !shared_clock_image(sav, rtc, 48, out, path, sizeof path));
}

int main(void) {
  assert(mkdtemp(dir));
  const char *const made[] = {"roms", "roms/gb", "roms/gba", "roms/gba/sub", "roms/nds", "roms/off", "saves", "saves/gb", "saves/gba",
                              "home", "home/.config", "home/.config/retroarch"};
  for (size_t i = 0; i < sizeof made / sizeof *made; i++)
    assert(!mkdir(in(made[i]), 0755));
  paths();
  retroarch();
  clocks();
  char cmd[4200];
  snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
  assert(!system(cmd));
  puts("shared saves ok");
  return 0;
}
