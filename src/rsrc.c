/* platform: host-agnostic
 * spec: the Resource Manager's file format (Inside Macintosh: More Macintosh Toolbox, "Resource Manager"): a 16-byte
 *       header (data offset, map offset, data length, map length); the map's type list at its offset 24, each type 8
 *       bytes (type, count-1, reference list offset); each reference 12 bytes (id, name offset, attributes and a 3-byte
 *       data offset); each resource's data a 4-byte length and the bytes. */
#include <errno.h>
#include <stdlib.h>
#include "vimg.h"

enum { MAX_MAP = 16 * 1024 * 1024 };

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

int vimg_rsrc_find(const vimg_io *rsrc, uint32_t type, int16_t id, uint64_t *off, uint32_t *len) {
  uint8_t h[16], l[4]; int e = -EINVAL;
  if (rsrc->size < 16 || rsrc->pread(rsrc->ctx, h, 16, 0)) return -EINVAL;
  uint32_t doff = be32(h), moff = be32(h + 4), dlen = be32(h + 8), mlen = be32(h + 12);
  if (mlen < 30 || mlen > MAX_MAP || (uint64_t)moff + mlen > rsrc->size || (uint64_t)doff + dlen > rsrc->size) return -EINVAL;
  uint8_t *m = malloc(mlen);
  if (!m) return -ENOMEM;
  if (rsrc->pread(rsrc->ctx, m, mlen, moff)) goto out;
  uint32_t tl = be16(m + 24);
  if (tl + 2 > mlen) goto out;
  uint32_t types = (uint32_t)be16(m + tl) + 1;
  if (be16(m + tl) == 0xffff) types = 0;
  for (uint32_t t = 0; t < types; t++) {
    uint32_t te = tl + 2 + 8 * t;
    if (te + 8 > mlen) goto out;
    if (be32(m + te) != type) continue;
    uint32_t refs = (uint32_t)be16(m + te + 4) + 1, rl = tl + be16(m + te + 6);
    for (uint32_t r = 0; r < refs; r++) {
      uint32_t re = rl + 12 * r;
      if (re + 12 > mlen) goto out;
      if ((int16_t)be16(m + re) != id) continue;
      uint32_t ro = be32(m + re + 4) & 0xffffff;
      if ((uint64_t)ro + 4 > dlen || rsrc->pread(rsrc->ctx, l, 4, (uint64_t)doff + ro)) goto out;
      uint32_t n = be32(l);
      if ((uint64_t)ro + 4 + n > dlen) goto out;
      *off = (uint64_t)doff + ro + 4; *len = n; e = 0; goto out;
    }
  }
out:
  free(m); return e;
}
