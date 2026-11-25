/* The companion's memory reader over armGBA: the same contract as pd_lr_mem.c's, read straight from the machine's
 * arrays so nothing a game could notice happens (a bus read costs cycles and moves the prefetcher). EWRAM, IWRAM and the
 * cartridge are what the companion reads; anything else says no. GPL-3.0, see ../NOTICE. */
#include "core.h"
#include "gba.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static const uint8_t *span(uint32_t a, size_t n) {
  if (!gba || !n)
    return NULL;
  const uint8_t *base;
  size_t size, off;
  switch (a >> 24) {
  case 0x02:
    base = gba->ewram;
    size = SIZE_EWRAM;
    off = (a & 0x00ffffffu) % SIZE_EWRAM;
    break;
  case 0x03:
    base = gba->iwram;
    size = SIZE_IWRAM;
    off = (a & 0x00ffffffu) % SIZE_IWRAM;
    break;
  case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d:
    base = gba->rom;
    size = gba->romSize;
    off = a & 0x01ffffffu;
    break;
  default:
    return NULL;
  }
  return base && off <= size && n <= size - off ? base + off : NULL;
}

bool pd_lr_read_memory(void *ctx, uint32_t a, void *out, size_t n) {
  (void)ctx;
  const uint8_t *p = span(a, n);
  if (p)
    memcpy(out, p, n);
  return p != NULL;
}

bool pd_lr_write_memory(void *ctx, uint32_t a, const void *in, size_t n) {
  (void)ctx;
  uint8_t *p = a >> 24 <= 3 ? (uint8_t *)span(a, n) : NULL;
  if (p)
    memcpy(p, in, n);
  return p != NULL;
}
