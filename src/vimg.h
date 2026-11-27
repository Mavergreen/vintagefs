/* platform: host-agnostic -- libvimg: classic Mac disk images (DiskCopy 4.2, NDIF), with no host dependency */
#ifndef VIMG_H
#define VIMG_H
#include <stddef.h>
#include <stdint.h>

/* An image file's bytes. pwrite NULL means read-only. Both return 0 or a negative errno. */
typedef struct {
  int (*pread)(void *ctx, void *buf, size_t len, uint64_t off);
  int (*pwrite)(void *ctx, const void *buf, size_t len, uint64_t off);
  uint64_t size;
  void *ctx;
} vimg_io;

typedef struct vimg_dc42 vimg_dc42;
/* 0, -EINVAL (not DiskCopy 4.2), -ENOMEM or the io's error */
int vimg_dc42_open(const vimg_io *io, vimg_dc42 **out);
uint64_t vimg_dc42_sectors(const vimg_dc42 *d);
/* 1 if the stored data checksum matches the data */
int vimg_dc42_checksum_ok(const vimg_dc42 *d);
int vimg_dc42_read(vimg_dc42 *d, void *buf, uint64_t sector, uint64_t count);
/* -EROFS when the io is read-only or the checksum was bad at open */
int vimg_dc42_write(vimg_dc42 *d, const void *buf, uint64_t sector, uint64_t count);
/* rewrites the header's data checksum when anything was written */
int vimg_dc42_flush(vimg_dc42 *d);
/* flushes, then frees */
void vimg_dc42_close(vimg_dc42 *d);

/* Finds resource (type, id) in a resource fork: the offset of its data and its length. 0 or -EINVAL. */
int vimg_rsrc_find(const vimg_io *rsrc, uint32_t type, int16_t id, uint64_t *off, uint32_t *len);

typedef struct vimg_ndif vimg_ndif;
/* data and resource forks; 0, -EINVAL (not NDIF, or Rken), -ENOTSUP (a chunk type no reader knows), -ENOMEM */
int vimg_ndif_open(const vimg_io *data, const vimg_io *rsrc, vimg_ndif **out);
uint64_t vimg_ndif_sectors(const vimg_ndif *n);
/* 1 if any chunk is ADC-compressed (ROCo) */
int vimg_ndif_compressed(const vimg_ndif *n);
int vimg_ndif_read(vimg_ndif *n, void *buf, uint64_t sector, uint64_t count);
void vimg_ndif_close(vimg_ndif *n);

/* Apple Data Compression; 0 when in decodes to exactly outlen bytes, else -EIO */
int vimg_adc_decode(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen);

#endif
