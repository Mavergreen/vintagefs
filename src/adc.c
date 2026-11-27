/* platform: host-agnostic -- copied from dmg-codecs (INGREDIENTS.md), renamed into libvimg */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include "vimg.h"

int vimg_adc_decode(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen) {
  size_t i = 0, o = 0;
  while (i < inlen && o < outlen) {
    uint8_t b = in[i++];
    size_t len, dist;
    if (b & 0x80) {  /* literal run */
      len = (size_t)(b & 0x7f) + 1;
      if (len > inlen - i || len > outlen - o) return -EIO;
      for (size_t k = 0; k < len; k++) out[o++] = in[i++];
      continue;
    }
    if (b & 0x40) {  /* 3-byte copy */
      if (inlen - i < 2) return -EIO;
      len = (size_t)(b & 0x3f) + 4; dist = ((size_t)in[i] << 8 | in[i + 1]) + 1; i += 2;
    } else {         /* 2-byte copy */
      if (inlen - i < 1) return -EIO;
      len = (size_t)((b & 0x3c) >> 2) + 3; dist = ((size_t)(b & 3) << 8 | in[i]) + 1; i += 1;
    }
    if (dist > o || len > outlen - o) return -EIO;
    for (size_t k = 0; k < len; k++, o++) out[o] = out[o - dist];  /* spec: copies may overlap their source */
  }
  return o == outlen ? 0 : -EIO;
}
