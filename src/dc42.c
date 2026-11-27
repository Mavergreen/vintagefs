/* platform: host-agnostic
 * spec: DiskCopy 4.2 (Apple's Disk Copy 4.2 format, as documented by Apple DTS and read by hfsutils and Basilisk II):
 *       an 84-byte big-endian header -- name length byte and name (to 63 bytes), data size at 0x40, tag size at 0x44,
 *       data checksum at 0x48, tag checksum at 0x4c, disk format at 0x50, format byte at 0x51, private word 0x0100 at
 *       0x52 -- then the data, then the tags. The checksum adds each big-endian 16-bit word and rotates right by one. */
#include <errno.h>
#include <stdlib.h>
#include "vimg.h"

struct vimg_dc42 { vimg_io io; uint32_t data; int sum_ok, dirty; };

enum { HDR = 84, CHUNK = 64 * 1024 };

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static int data_sum(const vimg_io *io, uint32_t len, uint32_t *out) {
  uint8_t *b = malloc(CHUNK); uint32_t s = 0;
  if (!b) return -ENOMEM;
  for (uint32_t off = 0; off < len; off += CHUNK) {
    uint32_t n = len - off < CHUNK ? len - off : CHUNK;
    int e = io->pread(io->ctx, b, n, HDR + (uint64_t)off);
    if (e) { free(b); return e; }
    for (uint32_t i = 0; i + 1 < n; i += 2) { s += (uint32_t)(b[i] << 8 | b[i + 1]); s = s >> 1 | s << 31; }
  }
  free(b); *out = s; return 0;
}

int vimg_dc42_open(const vimg_io *io, vimg_dc42 **out) {
  uint8_t h[HDR]; uint32_t s;
  *out = NULL;
  if (io->size < HDR) return -EINVAL;
  int e = io->pread(io->ctx, h, HDR, 0);
  if (e) return e;
  uint32_t ds = be32(h + 0x40), ts = be32(h + 0x44);
  if (h[0] > 63 || h[0x52] != 1 || h[0x53] != 0 || ds == 0 || ds % 512 || (uint64_t)HDR + ds + ts != io->size) return -EINVAL;
  if ((e = data_sum(io, ds, &s))) return e;
  vimg_dc42 *d = calloc(1, sizeof *d);
  if (!d) return -ENOMEM;
  d->io = *io; d->data = ds; d->sum_ok = s == be32(h + 0x48);
  *out = d; return 0;
}

uint64_t vimg_dc42_sectors(const vimg_dc42 *d) { return d->data / 512; }
int vimg_dc42_checksum_ok(const vimg_dc42 *d) { return d->sum_ok; }

static int in_range(const vimg_dc42 *d, uint64_t sector, uint64_t count) {
  uint64_t n = d->data / 512; return sector <= n && count <= n - sector;
}

int vimg_dc42_read(vimg_dc42 *d, void *buf, uint64_t sector, uint64_t count) {
  if (!in_range(d, sector, count)) return -EINVAL;
  return d->io.pread(d->io.ctx, buf, (size_t)(count * 512), HDR + sector * 512);
}

int vimg_dc42_writable(const vimg_dc42 *d) { return d->io.pwrite && d->sum_ok; }

int vimg_dc42_write(vimg_dc42 *d, const void *buf, uint64_t sector, uint64_t count) {
  if (!vimg_dc42_writable(d)) return -EROFS;
  if (!in_range(d, sector, count)) return -EINVAL;
  d->dirty = 1;
  return d->io.pwrite(d->io.ctx, buf, (size_t)(count * 512), HDR + sector * 512);
}

int vimg_dc42_flush(vimg_dc42 *d) {
  uint32_t s; uint8_t b[4];
  if (!d->dirty) return 0;
  int e = data_sum(&d->io, d->data, &s);
  if (e) return e;
  b[0] = s >> 24; b[1] = s >> 16; b[2] = s >> 8; b[3] = s;
  if ((e = d->io.pwrite(d->io.ctx, b, 4, 0x48))) return e;
  d->dirty = 0; return 0;
}

int vimg_dc42_sums(vimg_dc42 *d, uint32_t *stored, uint32_t *calculated) {
  uint8_t b[4];
  int e = d->io.pread(d->io.ctx, b, 4, 0x48);
  if (e) return e;
  *stored = be32(b);
  return data_sum(&d->io, d->data, calculated);
}

void vimg_dc42_close(vimg_dc42 *d) { if (d) { vimg_dc42_flush(d); free(d); } }
