/* Saves shared with the firmware's emulators (see shared_save.h). */
#include "shared_save.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pd_fs.h"

#ifdef _WIN32
#define lstat stat
#endif

#define MAX_GB (0x10000 + 512) // the biggest save and a clock: a bigger .sav / .srm is another console's game's
#define MAX_GBA (0x20000 + 0x2000) // VBA Next's has the EEPROM after the flash
#define MBC3_CLOCK 48u    // mGBA's GBMBCRTCSaveBuffer: 5 clock words, 5 latched ones, when they were latched
#define GAMBATTE_CLOCK 8u // when the clock read 0
#define DAY 86400
#define CLOCK_WRAP (0x1ff * (int64_t)DAY) // Gambatte's doLatch

static bool sharing;

static bool is_dir(const char *path) {
  struct stat st;
  return !stat(path, &st) && S_ISDIR(st.st_mode);
}

// RetroArch's setting, the first of its name. Empty = not set.
static void ra_setting(FILE *f, const char *key, char *out, size_t size) {
  char line[PATH_MAX + 64];
  size_t n = strlen(key);
  *out = 0;
  rewind(f);
  while (fgets(line, sizeof line, f)) {
    const char *value = line + strspn(line, " \t");
    if (strncmp(value, key, n) || !strspn(value + n, " =\t"))
      continue;
    value += n + strspn(value + n, " =\t");
    if (*value == '"')
      snprintf(out, size, "%.*s", (int)strcspn(value + 1, "\"\r\n"), value + 1);
    else
      snprintf(out, size, "%.*s", (int)strcspn(value, " \t\r\n"), value);
    return;
  }
}

static bool ra_is(FILE *f, const char *key, const char *word, const char *digit) {
  char value[8];
  ra_setting(f, key, value, sizeof value);
  return !strcmp(value, word) || !strcmp(value, digit);
}

// RetroArch's runloop_path_set_redirect. raDir is empty when saves are beside their games.
static bool raFound, raByContent;
static char raDir[PATH_MAX];
static void ra_find(void) {
  char base[PATH_MAX], path[PATH_MAX + 32], value[PATH_MAX];
  const char *home = getenv("HOME"), *config = getenv("XDG_CONFIG_HOME");
  raFound = false;
  if (!home)
    return;
  if (config && *config)
    snprintf(base, sizeof base, "%s/retroarch", config);
  else
    snprintf(base, sizeof base, "%s/.config/retroarch", home);
  snprintf(path, sizeof path, "%s/retroarch.cfg", base);
  FILE *f = fopen(path, "r");
  if (!f)
    return;
  ra_setting(f, "savefile_directory", value, sizeof value);
  int n;
  if (ra_is(f, "savefiles_in_content_dir", "true", "1"))
    n = raDir[0] = 0;
  else if (!*value || !strcmp(value, "default"))
    n = snprintf(raDir, sizeof raDir, "%s/saves", base);
  else
    n = snprintf(raDir, sizeof raDir, "%s%s", value[0] == '~' ? home : "", value + (value[0] == '~'));
  raByContent = ra_is(f, "sort_savefiles_by_content_enable", "true", "1");
  // RetroArch ignores a folder that isn't there. Sorted by core, which depends on the version when it isn't set, a game's
  // folder can't be known, and a compressed save can't be read.
  raFound = n < (int)sizeof raDir && (!raDir[0] || (raDir[0] == '/' && is_dir(raDir))) &&
            ra_is(f, "sort_savefiles_enable", "false", "0") && !ra_is(f, "save_file_compression", "true", "1");
  fclose(f);
}

bool shared_saves_find(void) {
  ra_find();
  return getenv("POCKET_ROM_DIRS") && (getenv("POCKET_SAVE_DIRS") || raFound);
}

void shared_saves_use(bool on) {
  sharing = on;
  ra_find();
}

// The i-th of a colon-separated list, empty ones counted. false = there are fewer.
static bool nth(const char *list, int i, char *out, size_t size) {
  for (; i > 0; i--) {
    list = strchr(list, ':');
    if (!list)
      return false;
    list++;
  }
  size_t n = strcspn(list, ":");
  while (n > 1 && list[n - 1] == '/')
    n--;
  return snprintf(out, size, "%.*s", (int)n, list) < (int)size;
}

// The folder of that name in dir, there or not: a FAT card's gba can be RetroArch's GBA.
static bool folder_in(const char *dir, const char *name, char *out, size_t size) {
  if (snprintf(out, size, "%s/%s", dir, name) >= (int)size)
    return false;
  DIR *d = is_dir(out) ? NULL : opendir(dir);
  struct dirent *e;
  while (d && (e = readdir(d)))
    if (!strcasecmp(e->d_name, name))
      memcpy(out + strlen(out) - strlen(name), e->d_name, strlen(name)); // as long
  if (d)
    closedir(d);
  return true;
}

// The save folder of the i-th ROM folder, as shared_save_path answers.
static int save_dir(int i, const char *romDir, bool ds, char *out, size_t size) {
  const char *listed = getenv("POCKET_SAVE_DIRS"), *dir = ds || !raDir[0] ? romDir : raDir;
  if (listed)
    return !nth(listed, i, out, size) || !out[0] ? 0 : is_dir(out) ? 1 : -1;
  if (!ds && !raFound) // a standalone DS emulator's is beside the game
    return -1;
  if (!ds && raByContent)
    return folder_in(dir, pd_base_name(romDir), out, size) ? 1 : -1;
  return snprintf(out, size, "%s", dir) < (int)size ? 1 : -1;
}

// 1 = a save, 0 = none there, 2 = one too big for the game (another console's), -1 = one that can't be used,
// RetroArch's compressed saves included.
static int look(const char *path, long max) {
  struct stat st;
  if (lstat(path, &st))
    return errno == ENOENT ? 0 : -1;
  char head[6] = {0};
  FILE *f = !stat(path, &st) && S_ISREG(st.st_mode) ? fopen(path, "rb") : NULL;
  size_t n = f ? fread(head, 1, sizeof head, f) : 0;
  if (f)
    fclose(f);
  if (!n || (n == sizeof head && !memcmp(head, "#RZIPv", sizeof head)))
    return -1;
  return st.st_size <= max ? 1 : 2;
}

// The console a ROM file's extension is for: 1 Game Boy, 2 GBA, 3 DS, 0 an archive (could be any), -1 not a ROM.
static int console_of(const char *ext) {
  if (!strcasecmp(ext, ".gb") || !strcasecmp(ext, ".gbc"))
    return 1;
  if (!strcasecmp(ext, ".gba"))
    return 2;
  if (!strcasecmp(ext, ".nds"))
    return 3;
  return !strcasecmp(ext, ".zip") || !strcasecmp(ext, ".7z") ? 0 : -1;
}

static bool same_dir(const char *a, const char *b) {
  char ra[PATH_MAX], rb[PATH_MAX];
  return !strcmp(a, b) || (realpath(a, ra) && realpath(b, rb) && !strcmp(ra, rb));
}

/* A game of another console with this one's base name in another ROM folder whose saves go to the same folder: RetroArch's
 * one flat save folder gives gb/X.gb and gba/X.gba one X.srm, so it could be that game's. An archive counts (its ROM could
 * be any console's). Probed by name, not listed: the library asks this for every game. */
static bool other_console(const char *roms, int self, const char *saveDir, const char *romName, const char *ext) {
  static const char *const exts[] = {".gb", ".gbc", ".gba", ".nds", ".zip", ".7z",
                                     ".GB", ".GBC", ".GBA", ".NDS", ".ZIP", ".7Z"};
  int mine = console_of(ext);
  char dir[PATH_MAX], theirs[PATH_MAX], file[PATH_MAX + 300];
  for (int j = 0; nth(roms, j, dir, sizeof dir); j++) {
    if (j == self || !dir[0])
      continue;
    for (size_t k = 0; k < sizeof exts / sizeof *exts; k++) {
      int console = console_of(exts[k]);
      if (console == mine ||
          snprintf(file, sizeof file, "%s/%.*s%s", dir, (int)(ext - romName), romName, exts[k]) >= (int)sizeof file ||
          access(file, F_OK))
        continue;
      for (int ds = 0; ds < 2; ds++)
        if ((!console || ds == (console == 3)) && save_dir(j, dir, ds, theirs, sizeof theirs) == 1 &&
            same_dir(theirs, saveDir))
          return true;
    }
  }
  return false;
}

int shared_save_path(const char *romPath, const char *romName, char *out, size_t size) {
  const char *roms = getenv("POCKET_ROM_DIRS");
  const char *name = pd_base_name(romPath), *ext = strrchr(romName, '.');
  if (!sharing || !roms || name == romPath || !ext || ext == romName)
    return 0;
  char romDir[PATH_MAX], saveDir[PATH_MAX], own[PATH_MAX];
  int n = (int)(name - romPath);
  while (n > 1 && romPath[n - 1] == '/')
    n--;
  if (snprintf(own, sizeof own, "%.*s", n, romPath) >= (int)sizeof own)
    return 0;
  int i = 0;
  do
    if (!nth(roms, i, romDir, sizeof romDir))
      return 0;
  while (strcmp(romDir, own) && ++i);
  bool ds = !strcasecmp(ext, ".nds");
  int found = save_dir(i, romDir, ds, saveDir, sizeof saveDir);
  if (found < 1)
    return found;
  // Another console's game would share the file: PokeDaisy's own save then, rather than a guess.
  if (other_console(roms, i, saveDir, romName, ext))
    return 0;
  const long max = ds ? LONG_MAX : strcasecmp(ext, ".gba") ? MAX_GB : MAX_GBA;
  const char *const exts[] = {"srm", "sav", "srm"}; // a new one is RetroArch's, or a standalone DS emulator's
  for (int e = 0; e < 2 + !ds; e++) {
    if (snprintf(out, size, "%s/%.*s.%s", saveDir, (int)(ext - romName), romName, exts[e]) >= (int)size)
      return -1;
    found = look(out, max);
    if (found < 0)
      return -1;
    if (found == 2) {
      if (strcmp(exts[e], "sav")) // a .srm too big: another console's game's, which this one would write over
        return 0;
      found = 0; // a .sav too big: another emulator's, left alone (the new save is a .srm)
      continue;
    }
    if (found)
      break;
  }
  if (pd_stem_shared_named(romPath, romName)) // it could be another ROM's
    return -found;
  return access(is_dir(saveDir) ? saveDir : raDir[0] ? raDir : romDir, W_OK) ? -1 : 1;
}

static uint32_t get32(const unsigned char *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int64_t get64(const unsigned char *p) { return (int64_t)(get32(p) | (uint64_t)get32(p + 4) << 32); }
static void put(unsigned char *p, uint64_t v, int bytes) {
  for (int i = 0; i < bytes; i++)
    p[i] = (unsigned char)(v >> (8 * i));
}

static bool clock_path(const char *savePath, char *out, size_t size) {
  const char *name = pd_base_name(savePath), *ext = strrchr(name, '.');
  return ext && snprintf(out, size, "%.*s.rtc", (int)(ext - savePath), savePath) < (int)size;
}

// The clock file's bytes: its size, 0 = no file or one that's neither's.
static size_t clock_read(const char *path, unsigned char *out, size_t n) {
  unsigned char b[MBC3_CLOCK + 1];
  FILE *f = fopen(path, "rb");
  size_t got = f ? fread(b, 1, sizeof b, f) : 0;
  if (f)
    fclose(f);
  if (n != MBC3_CLOCK || (got != MBC3_CLOCK && (got != GAMBATTE_CLOCK || get64(b) <= 0)))
    return 0;
  memcpy(out, b, got);
  return got;
}

bool shared_clock_load(const char *savePath, unsigned char *rtc, size_t n, int64_t now) {
  char path[PATH_MAX * 2];
  unsigned char b[MBC3_CLOCK];
  size_t got = clock_path(savePath, path, sizeof path) ? clock_read(path, b, n) : 0;
  if (got == MBC3_CLOCK)
    memcpy(rtc, b, MBC3_CLOCK);
  if (got != GAMBATTE_CLOCK)
    return got != 0;
  int64_t past = now > get64(b) ? now - get64(b) : 0;
  bool carry = past > CLOCK_WRAP;
  if (carry)
    past -= (past - 1) / CLOCK_WRAP * CLOCK_WRAP;
  uint32_t days = (uint32_t)(past / DAY);
  const uint32_t regs[5] = {(uint32_t)(past % 60), (uint32_t)(past / 60 % 60), (uint32_t)(past / 3600 % 24),
                            days & 0xff, days >> 8 | (carry ? 0x80u : 0)};
  for (int i = 0; i < 5; i++) {
    put(rtc + 4 * i, regs[i], 4);
    put(rtc + 20 + 4 * i, regs[i], 4);
  }
  put(rtc + 40, (uint64_t)now, 8);
  return true;
}

size_t shared_clock_image(const char *savePath, const unsigned char *rtc, size_t n, unsigned char *out, char *path,
                          size_t size) {
  unsigned char b[MBC3_CLOCK];
  size_t got = clock_path(savePath, path, size) ? clock_read(path, b, n) : 0;
  if (got == MBC3_CLOCK)
    memcpy(out, rtc, MBC3_CLOCK);
  if (got != GAMBATTE_CLOCK)
    return got;
  const unsigned char *latched = rtc + 20;
  uint32_t high = get32(latched + 16);
  int64_t when = get64(rtc + 40);
  if (high & 0x40 || !when) // stopped, which the file can't say, or never read
    return 0;
  int64_t past = get32(latched) + 60 * (int64_t)get32(latched + 4) + 3600 * (int64_t)get32(latched + 8) +
                 DAY * ((int64_t)(get32(latched + 12) & 0xff) + 256 * (int64_t)(high & 1));
  int64_t zero = when - past, off = zero - get64(b);
  if (get64(b) > when || (off >= -60 && off <= 60)) // the device's date is behind, or it's the same clock
    return 0;
  put(out, (uint64_t)zero, GAMBATTE_CLOCK);
  return GAMBATTE_CLOCK;
}
