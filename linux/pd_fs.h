/* Making a folder: POSIX mkdir takes a mode, Windows' _mkdir doesn't. */
#ifndef PD_FS_H
#define PD_FS_H

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#ifdef PD_UWP
#include "pd_uwp_fs.h"
#define fsync(fd) _commit(fd) // a save on its way to the drive: flush the handle
static inline int pd_mkdir(const char *path) {
  return pd_uwp_mkdir(path);
}
static inline int pd_rename(const char *from, const char *to) {
  return pd_uwp_rename(from, to);
}
#elif defined(_WIN32)
#include <direct.h>
#include <io.h>
#define fsync(fd) _commit(fd) // a save on its way to the card: Windows flushes a file handle with _commit
static inline int pd_mkdir(const char *path) {
  return _mkdir(path);
}
// POSIX rename replaces a file that is already there, Windows' plain rename refuses to: every write after the first one
// (a battery save, a state, host.ini) failed. MoveFileEx replaces it, and waits for the disk like fsync does.
// Declared here rather than through <windows.h>, whose min / max macros this frontend doesn't want.
__declspec(dllimport) int __stdcall MoveFileExA(const char *from, const char *to, unsigned long flags);
static inline int pd_rename(const char *from, const char *to) {
  return MoveFileExA(from, to, 0x1ul /* REPLACE_EXISTING */ | 0x8ul /* WRITE_THROUGH */) ? 0 : -1;
}
#else
static inline int pd_mkdir(const char *path) {
  return mkdir(path, 0755);
}
#define pd_rename(from, to) rename(from, to)
#endif

/* Another ROM beside romPath with romName's stem (X.gb beside X.gba): then a save named after the stem could be either's.
 * romName is romPath's own file name, or the name a zipped ROM's saves go by (rom_zip.h); romPath itself never counts. */
static inline int pd_stem_shared_named(const char *romPath, const char *romName) {
  const char *slash = strrchr(romPath, '/'), *back = strrchr(romPath, '\\');
  if (back && (!slash || back > slash))
    slash = back;
  const char *self = slash ? slash + 1 : romPath, *dot = strrchr(romName, '.');
  if (!dot || dot == romName)
    return 0;
  char dir[4096];
  snprintf(dir, sizeof dir, "%.*s", slash ? (int)(slash - romPath) : 1, slash ? romPath : ".");
  DIR *d = opendir(dir);
  if (!d)
    return 1; // can't tell: don't guess
  int shared = 0;
  struct dirent *e;
  static const char *const exts[] = {".gb", ".gbc", ".gba", ".nds", ".zip", ".7z"};
  while (!shared && (e = readdir(d))) {
    const char *edot = strrchr(e->d_name, '.');
    if (!edot || edot - e->d_name != dot - romName || strncmp(e->d_name, romName, (size_t)(dot - romName)) ||
        !strcmp(e->d_name, romName) || !strcmp(e->d_name, self))
      continue;
    for (size_t k = 0; k < sizeof exts / sizeof *exts; k++)
      if (!strcasecmp(edot, exts[k]))
        shared = 1;
  }
  closedir(d);
  return shared;
}
static inline int pd_stem_shared(const char *romPath) {
  const char *slash = strrchr(romPath, '/'), *back = strrchr(romPath, '\\');
  if (back && (!slash || back > slash))
    slash = back;
  return pd_stem_shared_named(romPath, slash ? slash + 1 : romPath);
}

static inline const char *pd_base_name(const char *path) {
  const char *slash = strrchr(path, '/'), *back = strrchr(path, '\\');
  if (back && (!slash || back > slash))
    slash = back;
  return slash ? slash + 1 : path;
}

/* The battery save in DIR for the ROM at romPath, named the way Android's PokeDaisy, the 3DS port and other emulators
 * name it: DIR/<base>.sav, the ROM's name without its extension (FireRed.gba -> FireRed.sav), so one save syncs between
 * them unrenamed. Up to 1.6.2 this host wrote DIR/<name>.sav (FireRed.gba.sav), which is still read: when only that one
 * is there it stays the save, and when both are, the one written last is (a copy synced in from a phone is newer than
 * the one the host wrote before). Another ROM beside it with the same base name (FireRed.gb, FireRed.gba) keeps the
 * whole name: <base>.sav could be either's. [out] may name a file that doesn't exist yet: the name this host writes.
 * romName: romPath's own, or the name a zipped ROM's saves go by (rom_save_name in rom_zip.h). */
static inline void pd_save_path_named(const char *dir, const char *romPath, const char *romName, char *out, size_t size) {
  snprintf(out, size, "%s/%s.sav", dir, romName);
  const char *dot = strrchr(romName, '.');
  if (!dot || dot == romName || pd_stem_shared_named(romPath, romName))
    return;
  char base[4096];
  int n = snprintf(base, sizeof base, "%s/%.*s.sav", dir, (int)(dot - romName), romName);
  if (n < 0 || (size_t)n >= sizeof base || (size_t)n >= size)
    return; // too long to name: keep <romName>.sav
  struct stat whole, plain;
  bool haveWhole = !stat(out, &whole), havePlain = !stat(base, &plain);
  if (haveWhole && (!havePlain || whole.st_mtime > plain.st_mtime))
    return; // only the old name, or it was written last
  memcpy(out, base, (size_t)n + 1);
}
static inline void pd_save_path(const char *dir, const char *romPath, char *out, size_t size) {
  pd_save_path_named(dir, romPath, pd_base_name(romPath), out, size);
}

#endif
