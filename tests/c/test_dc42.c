/* platform: host-agnostic -- DiskCopy 4.2: valid images, writes and their checksums, then malformed headers */
#include <pthread.h>
#include <stdlib.h>
#include "check.h"
#include "mem.h"

static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
/* the test's own copy of DiskCopy 4.2's checksum, so a wrong one in src/ can't agree with itself */
static uint32_t sum(const uint8_t *p, size_t n) {
  uint32_t s = 0;
  for (size_t i = 0; i + 1 < n; i += 2) { s += (uint32_t)(p[i] << 8 | p[i + 1]); s = s >> 1 | s << 31; }
  return s;
}
/* an image of `sectors` data sectors and `tag` tag bytes per sector, each data byte patterned */
static uint8_t *mk(uint32_t sectors, uint32_t tag, uint8_t fmt, size_t *n) {
  uint32_t ds = sectors * 512, ts = sectors * tag;
  *n = 84 + ds + ts; uint8_t *p = calloc(1, *n);
  p[0] = 7; memcpy(p + 1, "CLASSIC", 7);
  put32(p + 0x40, ds); put32(p + 0x44, ts);
  for (uint32_t i = 0; i < ds; i++) p[84 + i] = (uint8_t)(i * 7 + i / 512);
  for (uint32_t i = 0; i < ts; i++) p[84 + ds + i] = (uint8_t)(i * 3 + 1);
  put32(p + 0x48, sum(p + 84, ds));
  put32(p + 0x4c, ts > 12 ? sum(p + 84 + ds + 12, ts - 12) : 0);
  p[0x50] = fmt; p[0x51] = 0x22; p[0x52] = 1; p[0x53] = 0;
  return p;
}

static void valid_800k(void) {
  size_t n; uint8_t *p = mk(1600, 0, 1, &n); mem_buf m = { p, n }; vimg_io io = mem_io(&m, 1); vimg_dc42 *d = NULL;
  CHECK_EQ(vimg_dc42_open(&io, &d), 0);
  if (d) {
    CHECK_EQ(vimg_dc42_sectors(d), 1600);
    CHECK_EQ(vimg_dc42_checksum_ok(d), 1);
    CHECK_EQ(vimg_dc42_writable(d), 1);
    uint32_t st = 0, ca = 0; CHECK_EQ(vimg_dc42_sums(d, &st, &ca), 0);
    CHECK_EQ(st, get32(p + 0x48)); CHECK_EQ(ca, st);
    uint8_t buf[1024]; CHECK_EQ(vimg_dc42_read(d, buf, 3, 2), 0);
    CHECK(memcmp(buf, p + 84 + 3 * 512, 1024) == 0);
    CHECK_EQ(vimg_dc42_read(d, buf, 1599, 2), -EINVAL);
    CHECK_EQ(vimg_dc42_read(d, buf, 1600, 1), -EINVAL);
    vimg_dc42_close(d);
  }
  free(p);
}

static void write_updates_checksum(uint32_t tag, uint8_t fmt) {
  size_t n; uint8_t *p = mk(800, tag, fmt, &n); mem_buf m = { p, n }; vimg_io io = mem_io(&m, 1); vimg_dc42 *d = NULL;
  uint8_t *tags = malloc(800 * tag + 1); memcpy(tags, p + 84 + 800 * 512, 800 * tag);
  uint32_t tagsum = get32(p + 0x4c), before = get32(p + 0x48);
  CHECK_EQ(vimg_dc42_open(&io, &d), 0);
  if (d) {
    uint8_t s[512]; memset(s, 0xa5, sizeof s);
    CHECK_EQ(vimg_dc42_write(d, s, 100, 1), 0);
    CHECK(memcmp(p + 84 + 100 * 512, s, 512) == 0);
    CHECK_EQ(vimg_dc42_flush(d), 0);
    CHECK_EQ(get32(p + 0x48), sum(p + 84, 800 * 512));
    CHECK(get32(p + 0x48) != before);
    CHECK_EQ(get32(p + 0x4c), tagsum);
    CHECK(memcmp(p + 84 + 800 * 512, tags, 800 * tag) == 0);
    CHECK_EQ(vimg_dc42_write(d, s, 800, 1), -EINVAL);
    memset(s, 0x3c, sizeof s);
    CHECK_EQ(vimg_dc42_write(d, s, 799, 1), 0);
    vimg_dc42_close(d);  /* close flushes */
    CHECK_EQ(get32(p + 0x48), sum(p + 84, 800 * 512));
  }
  free(tags); free(p);
}

static void bad_checksum_is_read_only(void) {
  size_t n; uint8_t *p = mk(1600, 0, 1, &n); put32(p + 0x48, get32(p + 0x48) ^ 1);
  mem_buf m = { p, n }; vimg_io io = mem_io(&m, 1); vimg_dc42 *d = NULL;
  CHECK_EQ(vimg_dc42_open(&io, &d), 0);
  if (d) {
    CHECK_EQ(vimg_dc42_checksum_ok(d), 0);
    CHECK_EQ(vimg_dc42_writable(d), 0);
    uint32_t st = 0, ca = 0; CHECK_EQ(vimg_dc42_sums(d, &st, &ca), 0);
    CHECK_EQ(st, get32(p + 0x48)); CHECK_EQ(ca, sum(p + 84, 1600 * 512)); CHECK(st != ca);
    uint8_t s[512] = { 0 }; CHECK_EQ(vimg_dc42_write(d, s, 0, 1), -EROFS);
    CHECK_EQ(vimg_dc42_read(d, s, 0, 1), 0);
    vimg_dc42_close(d);
  }
  free(p);
}

static void read_only_io(void) {
  size_t n; uint8_t *p = mk(1600, 0, 1, &n); mem_buf m = { p, n }; vimg_io io = mem_io(&m, 0); vimg_dc42 *d = NULL;
  CHECK_EQ(vimg_dc42_open(&io, &d), 0);
  if (d) { uint8_t s[512] = { 0 }; CHECK_EQ(vimg_dc42_writable(d), 0); CHECK_EQ(vimg_dc42_write(d, s, 0, 1), -EROFS); vimg_dc42_close(d); }
  free(p);
}

static int open_mutated(void (*mutate)(uint8_t *, size_t *)) {
  size_t n; uint8_t *p = mk(1600, 0, 1, &n); mutate(p, &n);
  mem_buf m = { p, n }; vimg_io io = mem_io(&m, 0); vimg_dc42 *d = NULL;
  int e = vimg_dc42_open(&io, &d); if (d) vimg_dc42_close(d); free(p); return e;
}
static void m_private(uint8_t *p, size_t *n) { p[0x52] = 0; }
static void m_namelen(uint8_t *p, size_t *n) { p[0] = 64; }
static void m_odd_data(uint8_t *p, size_t *n) { put32(p + 0x40, 1600 * 512 - 4); put32(p + 0x44, 4); }
static void m_size(uint8_t *p, size_t *n) { *n -= 1; }
static void m_short(uint8_t *p, size_t *n) { *n = 83; }
static void m_huge(uint8_t *p, size_t *n) { put32(p + 0x40, 0xfffffe00); }

static void malformed(void) {
  CHECK_EQ(open_mutated(m_private), -EINVAL);
  CHECK_EQ(open_mutated(m_namelen), -EINVAL);
  CHECK_EQ(open_mutated(m_odd_data), -EINVAL);
  CHECK_EQ(open_mutated(m_size), -EINVAL);
  CHECK_EQ(open_mutated(m_short), -EINVAL);
  CHECK_EQ(open_mutated(m_huge), -EINVAL);
}

/* the header is current after every write, before any flush: a crash or kill then leaves a valid image */
static void checksum_current_without_flush(void) {
  size_t n; uint8_t *p = mk(1600, 0, 1, &n); mem_buf m = { p, n }; vimg_io io = mem_io(&m, 1); vimg_dc42 *d = NULL;
  CHECK_EQ(vimg_dc42_open(&io, &d), 0);
  if (d) {
    uint8_t s[1024]; memset(s, 0x5e, sizeof s);
    CHECK_EQ(vimg_dc42_write(d, s, 7, 2), 0);
    CHECK_EQ(get32(p + 0x48), sum(p + 84, 1600 * 512));
    memset(s, 0x11, sizeof s);
    CHECK_EQ(vimg_dc42_write(d, s, 1000, 1), 0);
    CHECK_EQ(get32(p + 0x48), sum(p + 84, 1600 * 512));
    free(d);  /* no flush, no close: as if the process died here */
  }
  free(p);
}

typedef struct { vimg_dc42 *d; int k; } job;
static void *writer(void *v) {
  job *j = v; uint8_t s[512];
  for (int i = 0; i < 200; i++) { memset(s, j->k * 31 + i, sizeof s); vimg_dc42_write(j->d, s, (uint64_t)(j->k * 200 + i), 1); }
  return NULL;
}
static void *flusher(void *v) { for (int i = 0; i < 200; i++) vimg_dc42_flush(v); return NULL; }
/* writes and flushes from several threads at once, as the framework may issue them */
static void concurrent_writes_and_flushes(void) {
  size_t n; uint8_t *p = mk(1600, 0, 1, &n); mem_buf m = { p, n }; vimg_io io = mem_io(&m, 1); vimg_dc42 *d = NULL;
  CHECK_EQ(vimg_dc42_open(&io, &d), 0);
  if (d) {
    pthread_t t[5]; job j[4];
    for (int k = 0; k < 4; k++) { j[k] = (job){ d, k }; pthread_create(&t[k], NULL, writer, &j[k]); }
    pthread_create(&t[4], NULL, flusher, d);
    for (int k = 0; k < 5; k++) pthread_join(t[k], NULL);
    vimg_dc42_close(d);
    CHECK_EQ(get32(p + 0x48), sum(p + 84, 1600 * 512));
  }
  free(p);
}

static void stored_sum_cheaply(void) {
  size_t n; uint8_t *p = mk(1600, 0, 1, &n); mem_buf m = { p, n }; vimg_io io = mem_io(&m, 0); vimg_dc42 *d = NULL;
  CHECK_EQ(vimg_dc42_open(&io, &d), 0);
  if (d) { uint32_t st = 0; CHECK_EQ(vimg_dc42_stored_sum(d, &st), 0); CHECK_EQ(st, get32(p + 0x48)); vimg_dc42_close(d); }
  free(p);
}

int main(void) {
  checksum_current_without_flush();
  concurrent_writes_and_flushes();
  stored_sum_cheaply();
  valid_800k();
  write_updates_checksum(0, 1);
  write_updates_checksum(12, 0);  /* a 400K GCR disk: 12 tag bytes per sector */
  bad_checksum_is_read_only();
  read_only_io();
  malformed();
  return CHECK_DONE();
}
