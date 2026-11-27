/* platform: host-agnostic -- an in-memory vimg_io for the tests */
#ifndef VF_MEM_H
#define VF_MEM_H
#include <errno.h>
#include <string.h>
#include "vimg.h"
typedef struct { uint8_t *p; size_t n; } mem_buf;
static int mem_pread(void *c, void *buf, size_t len, uint64_t off) {
  mem_buf *m = c; if (off > m->n || len > m->n - off) return -EIO; memcpy(buf, m->p + off, len); return 0; }
static int mem_pwrite(void *c, const void *buf, size_t len, uint64_t off) {
  mem_buf *m = c; if (off > m->n || len > m->n - off) return -EIO; memcpy(m->p + off, buf, len); return 0; }
static vimg_io mem_io(mem_buf *m, int writable) {
  vimg_io io = { mem_pread, writable ? mem_pwrite : NULL, m->n, m }; return io; }
#endif
