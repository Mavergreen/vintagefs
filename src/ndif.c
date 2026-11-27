/* platform: host-agnostic
 * spec: NDIF (Disk Copy 6) keeps its disk in the data fork and its block map in the resource fork, as resource 'bcem'
 *       ID 128. Read from 10.9's own RdWr, Rdxx, ROCo and Rken output (tests/fixtures/make-ndif.sh): the map's entry
 *       count at bcem offset 0x7c, then 12-byte entries -- a 3-byte start sector, a 1-byte chunk type, a 4-byte data
 *       fork offset and a 4-byte length -- in ascending sector order from 0, ending with type 0xff at the disk's sector
 *       count. Types: 0x00 zeros, 0x02 raw, 0x83 ADC (ROCo), 0x80 KenCode (Rken, not read here). */
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "vimg.h"

enum { T_ZERO = 0x00, T_RAW = 0x02, T_KEN = 0x80, T_ADC = 0x83, T_END = 0xff,
       BCEM = 0x6263656d, MAP = 0x80, MAX_BCEM = 16 * 1024 * 1024,
       MAX_ADC_SECTORS = 4096 /* 2 MiB out; 10.9 writes 512 */, MAX_ADC_IN = 4 * 1024 * 1024 };

typedef struct { uint32_t sector, count, off, len; uint8_t type; } chunk;

struct vimg_ndif {
  vimg_io data; chunk *c; uint32_t n, sectors; int compressed;
  pthread_mutex_t lock; uint8_t *in, *out; uint32_t cached; /* index of the decoded chunk in out, or n */
};

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static int parse(vimg_ndif *d, const uint8_t *b, uint32_t blen) {
  if (blen < MAP) return -EINVAL;
  uint32_t entries = be32(b + MAP - 4);
  if (entries < 2 || entries > (blen - MAP) / 12) return -EINVAL;
  if (!(d->c = calloc(entries, sizeof *d->c))) return -ENOMEM;
  for (uint32_t i = 0; i < entries; i++) {
    const uint8_t *e = b + MAP + 12 * i;
    uint32_t sector = (uint32_t)e[0] << 16 | (uint32_t)e[1] << 8 | e[2];
    uint8_t type = e[3];
    if (i == 0 ? sector != 0 : sector <= d->c[i - 1].sector) return -EINVAL;
    if (i > 0) d->c[i - 1].count = sector - d->c[i - 1].sector;
    if (i == entries - 1) { if (type != T_END) return -EINVAL; d->sectors = sector; break; }
    switch (type) {
      case T_ZERO: case T_RAW: case T_ADC: break;
      case T_KEN: case T_END: return -EINVAL;
      default: return -ENOTSUP;
    }
    d->c[i] = (chunk){ sector, 0, be32(e + 4), be32(e + 8), type };
  }
  d->n = entries - 1;
  for (uint32_t i = 0; i < d->n; i++) {
    chunk *c = &d->c[i];
    if (c->type == T_ZERO) continue;
    if ((uint64_t)c->off + c->len > d->data.size) return -EINVAL;
    if (c->type == T_RAW && (uint64_t)c->len != (uint64_t)c->count * 512) return -EINVAL;
    if (c->type == T_ADC) {
      if (c->count > MAX_ADC_SECTORS || c->len == 0 || c->len > MAX_ADC_IN) return -EINVAL;
      d->compressed = 1;
    }
  }
  return 0;
}

int vimg_ndif_open(const vimg_io *data, const vimg_io *rsrc, vimg_ndif **out) {
  uint64_t off; uint32_t len; int e;
  *out = NULL;
  if ((e = vimg_rsrc_find(rsrc, BCEM, 128, &off, &len))) return e == -ENOMEM ? e : -EINVAL;
  if (len > MAX_BCEM) return -EINVAL;
  uint8_t *b = malloc(len ? len : 1);
  vimg_ndif *d = calloc(1, sizeof *d);
  if (!b || !d) { free(b); free(d); return -ENOMEM; }
  d->data = *data;
  e = rsrc->pread(rsrc->ctx, b, len, off) ? -EINVAL : parse(d, b, len);
  free(b);
  if (!e && d->compressed && (!(d->in = malloc(MAX_ADC_IN)) || !(d->out = malloc(MAX_ADC_SECTORS * 512)))) e = -ENOMEM;
  if (e) { free(d->in); free(d->out); free(d->c); free(d); return e; }
  d->cached = d->n;
  pthread_mutex_init(&d->lock, NULL);
  *out = d; return 0;
}

uint64_t vimg_ndif_sectors(const vimg_ndif *d) { return d->sectors; }
int vimg_ndif_compressed(const vimg_ndif *d) { return d->compressed; }

static uint32_t find(const vimg_ndif *d, uint64_t sector) {
  uint32_t lo = 0, hi = d->n;
  while (hi - lo > 1) { uint32_t mid = lo + (hi - lo) / 2; if (d->c[mid].sector <= sector) lo = mid; else hi = mid; }
  return lo;
}

static int adc(vimg_ndif *d, uint32_t i, uint8_t *buf, uint64_t from, uint64_t count) {
  chunk *c = &d->c[i]; int e = 0;
  pthread_mutex_lock(&d->lock);
  if (d->cached != i) {
    d->cached = d->n;
    if ((e = d->data.pread(d->data.ctx, d->in, c->len, c->off)) == 0 &&
        (e = vimg_adc_decode(d->in, c->len, d->out, (size_t)c->count * 512)) == 0) d->cached = i;
  }
  if (!e) memcpy(buf, d->out + from * 512, (size_t)count * 512);
  pthread_mutex_unlock(&d->lock);
  return e ? -EIO : 0;
}

int vimg_ndif_read(vimg_ndif *d, void *buf, uint64_t sector, uint64_t count) {
  uint8_t *p = buf;
  if (sector > d->sectors || count > d->sectors - sector) return -EINVAL;
  while (count) {
    uint32_t i = find(d, sector); chunk *c = &d->c[i];
    uint64_t from = sector - c->sector, k = c->count - from; int e = 0;
    if (k > count) k = count;
    switch (c->type) {
      case T_ZERO: memset(p, 0, (size_t)k * 512); break;
      case T_RAW: e = d->data.pread(d->data.ctx, p, (size_t)k * 512, c->off + from * 512); break;
      default: e = adc(d, i, p, from, k); break;
    }
    if (e) return e;
    p += k * 512; sector += k; count -= k;
  }
  return 0;
}

void vimg_ndif_close(vimg_ndif *d) {
  if (!d) return;
  pthread_mutex_destroy(&d->lock); free(d->in); free(d->out); free(d->c); free(d);
}
