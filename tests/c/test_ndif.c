/* platform: host-agnostic -- NDIF: 10.9's own RdWr, Rdxx, ROCo and Rken against what 10.9 reads them as, then damage */
#include <stdlib.h>
#include "check.h"
#include "mem.h"
#include "sha256.h"
#include "slurp.h"

static const char *FIX;
typedef struct { uint8_t *data, *rsrc; size_t dn, rn; mem_buf d, r; vimg_io dio, rio; } forks;

static int load(const char *fmt, forks *f) {
  char n[64];
  snprintf(n, sizeof n, "ndif/%s.data", fmt); f->data = slurp(FIX, n, &f->dn);
  snprintf(n, sizeof n, "ndif/%s.rsrc", fmt); f->rsrc = slurp(FIX, n, &f->rn);
  if (!f->data || !f->rsrc) return -1;
  f->d = (mem_buf){ f->data, f->dn }; f->r = (mem_buf){ f->rsrc, f->rn };
  f->dio = mem_io(&f->d, 0); f->rio = mem_io(&f->r, 0); return 0;
}
static void unload(forks *f) { free(f->data); free(f->rsrc); }

/* expected.txt: "<format> <sha256> <bytes>" per line, from tests/fixtures/make-ndif.sh */
static int expected(const char *fmt, char sha[65], uint64_t *bytes) {
  size_t n; uint8_t *t = slurp(FIX, "ndif/expected.txt", &n); char *line; int found = 0;
  if (!t) return 0;
  t = realloc(t, n + 1); t[n] = 0;
  for (line = strtok((char *)t, "\n"); line; line = strtok(NULL, "\n")) {
    char f[16]; unsigned long long b;
    if (sscanf(line, "%15s %64s %llu", f, sha, &b) == 3 && !strcmp(f, fmt)) { *bytes = b; found = 1; break; }
  }
  free(t); return found;
}

static uint8_t *flat;  /* RdWr's whole disk, the reference for spot reads */

static void reads_as_10_9_does(const char *fmt, int compressed) {
  forks f; vimg_ndif *n = NULL; char want[65], got[65]; uint64_t bytes = 0;
  if (load(fmt, &f) || !expected(fmt, want, &bytes)) { CHECK(0); return; }
  CHECK_EQ(vimg_ndif_open(&f.dio, &f.rio, &n), 0);
  if (n) {
    CHECK_EQ(vimg_ndif_sectors(n) * 512, bytes);
    CHECK_EQ(vimg_ndif_compressed(n), compressed);
    uint8_t *all = malloc(bytes); sha256 s;
    CHECK_EQ(vimg_ndif_read(n, all, 0, bytes / 512), 0);
    sha_init(&s); sha_update(&s, all, bytes); sha_hex(&s, got);
    CHECK(!strcmp(got, want));
    if (!flat) flat = all; else {
      /* spot reads: across chunk boundaries, from inside a chunk, from zero chunks, single sectors */
      static const uint64_t at[][2] = { { 510, 5 }, { 511, 2 }, { 1, 1 }, { 1967, 3 }, { 2000, 10 }, { 2397, 3 }, { 0, 2400 } };
      for (size_t i = 0; i < sizeof at / sizeof at[0]; i++) {
        uint8_t *b = malloc(at[i][1] * 512);
        CHECK_EQ(vimg_ndif_read(n, b, at[i][0], at[i][1]), 0);
        CHECK(memcmp(b, flat + at[i][0] * 512, at[i][1] * 512) == 0);
        free(b);
      }
      free(all);
    }
    uint8_t b[512];
    CHECK_EQ(vimg_ndif_read(n, b, 2400, 1), -EINVAL);
    CHECK_EQ(vimg_ndif_read(n, b, 2399, 2), -EINVAL);
    vimg_ndif_close(n);
  }
  unload(&f);
}

static int open_forks(forks *f) { vimg_ndif *n = NULL; int e = vimg_ndif_open(&f->dio, &f->rio, &n); if (n) vimg_ndif_close(n); return e; }

static void declined(void) {
  forks f;
  if (load("Rken", &f)) { CHECK(0); return; }
  CHECK_EQ(open_forks(&f), -EINVAL);
  unload(&f);
  if (load("RdWr", &f)) { CHECK(0); return; }
  f.r.n = 0; f.rio = mem_io(&f.r, 0);
  CHECK_EQ(open_forks(&f), -EINVAL);  /* the data fork alone, as after a copy through a non-Mac system */
  f.r.n = 100; f.rio = mem_io(&f.r, 0);
  CHECK_EQ(open_forks(&f), -EINVAL);  /* a truncated resource fork */
  f.r.n = f.rn; f.rio = mem_io(&f.r, 0);
  CHECK_EQ(open_forks(&f), 0);
  unload(&f);
}

/* the bcem resource's data starts 4 bytes into the resource data area; its block map at +0x80, 12 bytes an entry */
static uint8_t *entry(forks *f, int i) {
  uint32_t doff = (uint32_t)f->rsrc[0] << 24 | f->rsrc[1] << 16 | f->rsrc[2] << 8 | f->rsrc[3];
  return f->rsrc + doff + 4 + 0x80 + 12 * i;
}

static void damaged(void) {
  forks f;
  if (load("RdWr", &f)) { CHECK(0); return; }
  uint8_t keep[12]; memcpy(keep, entry(&f, 0), 12);
  entry(&f, 0)[11] += 1;                     /* the raw chunk runs one byte past the data fork */
  CHECK_EQ(open_forks(&f), -EINVAL);
  memcpy(entry(&f, 0), keep, 12);
  entry(&f, 0)[3] = 0x42;                    /* a chunk type no NDIF writer used */
  CHECK_EQ(open_forks(&f), -ENOTSUP);
  memcpy(entry(&f, 0), keep, 12);
  entry(&f, 0)[2] = 1;                       /* the map no longer starts at sector 0 */
  CHECK_EQ(open_forks(&f), -EINVAL);
  memcpy(entry(&f, 0), keep, 12);
  entry(&f, -1)[11] = 0xff;                  /* an entry count far past the resource */
  CHECK_EQ(open_forks(&f), -EINVAL);
  entry(&f, -1)[11] = 2;
  CHECK_EQ(open_forks(&f), 0);
  unload(&f);
  if (load("ROCo", &f)) { CHECK(0); return; }
  memset(f.data + 0x302a, 0x00, 0x400);      /* the second ADC chunk's start, garbled */
  vimg_ndif *n = NULL;
  CHECK_EQ(vimg_ndif_open(&f.dio, &f.rio, &n), 0);
  if (n) {
    uint8_t b[512];
    CHECK_EQ(vimg_ndif_read(n, b, 0, 1), 0);
    CHECK(vimg_ndif_read(n, b, 512, 1) != 0);  /* fails that read only */
    CHECK_EQ(vimg_ndif_read(n, b, 1, 1), 0);
    vimg_ndif_close(n);
  }
  unload(&f);
}

int main(int argc, char **argv) {
  FIX = argc > 1 ? argv[1] : "tests/fixtures";
  reads_as_10_9_does("RdWr", 0);
  reads_as_10_9_does("Rdxx", 0);
  reads_as_10_9_does("ROCo", 1);
  free(flat);
  declined();
  damaged();
  return CHECK_DONE();
}
