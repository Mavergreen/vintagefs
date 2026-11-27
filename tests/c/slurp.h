/* platform: host-agnostic -- reads a fixture file whole */
#ifndef VF_SLURP_H
#define VF_SLURP_H
#include <stdio.h>
#include <stdlib.h>
static uint8_t *slurp(const char *dir, const char *name, size_t *n) {
  char path[1024]; snprintf(path, sizeof path, "%s/%s", dir, name);
  FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", path); return NULL; }
  fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t *p = malloc(len ? (size_t)len : 1);
  if (p && fread(p, 1, (size_t)len, f) != (size_t)len) { free(p); p = NULL; }
  fclose(f); *n = (size_t)len; return p;
}
#endif
