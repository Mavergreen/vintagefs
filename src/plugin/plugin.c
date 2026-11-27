/* VintageImages.plugin: a DiskImages format plug-in that claims DiskCopy 4.2 and NDIF (RdWr, Rdxx, ROCo) images, which
 * 10.9 otherwise refuses, and serves their sectors through libvimg. Every other image is declined, so 10.9 treats it
 * exactly as before. DiskCopy 4.2 opens writable when its file is; NDIF opens read-only. */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <syslog.h>
#include "di_interface.h"
#include "vimg.h"

#ifdef VF_TRACE
#define TRACE(...) syslog(LOG_NOTICE, "VintageImages: " __VA_ARGS__)
#else
#define TRACE(...) ((void)0)
#endif

/* platform: the framework's probe scores; the highest wins, and 10.9's own DC42 and NDIF classes refuse these files
 *           while their legacy formats are switched off, which no preference turns on */
#define SCORE_CLAIM 1001
#define SCORE_DECLINE (-1000)
/* platform: the Finder type 10.9's hdiutil gives NDIF read/write images ('rohd' for Rdxx and ROCo) */
#define TYPE_DIMG 0x64696d67u

typedef struct {
  const DIPlugInInterface *vtbl;
  int32_t refs;
  CFUUIDRef factory;
  vimg_dc42 *dc;
  vimg_ndif *nd;
  CBackingStore *open_store;  /* the file, held open while this instance is an image */
  uint32_t file_type;
} instance;

static DIBackingStoreGetDataForkLength_fn get_length;
static DIBackingStoreReadDataFork_fn read_fork;
static DIBackingStoreWriteDataFork_fn write_fork;
static DIBackingStoreGetBaseBackingStore_fn get_base;
static DIBackingStoreOpenDataFork_fn open_fork;
static DIBackingStoreCloseDataFork_fn close_fork;
static DIBackingStoreIsWriteEnabled_fn write_enabled;
static DIBackingStoreOpenRsrcFork_fn open_rsrc;
static DIBackingStoreCloseRsrcFork_fn close_rsrc;
static DIBackingStoreGetRsrcForkLength_fn get_rsrc_length;
static DIBackingStoreReadRsrcFork_fn read_rsrc;
static DIBackingStoreGetFileType_fn get_file_type;
#define FIND(v, name) if (!v) v = (__typeof__(v))dlsym(RTLD_DEFAULT, name)
static int resolve(void) {
  FIND(get_length, "DIBackingStoreGetDataForkLength"); FIND(read_fork, "DIBackingStoreReadDataFork");
  FIND(write_fork, "DIBackingStoreWriteDataFork"); FIND(get_base, "DIBackingStoreGetBaseBackingStore");
  FIND(open_fork, "DIBackingStoreOpenDataFork"); FIND(close_fork, "DIBackingStoreCloseDataFork");
  FIND(write_enabled, "DIBackingStoreIsWriteEnabled"); FIND(open_rsrc, "DIBackingStoreOpenRsrcFork");
  FIND(close_rsrc, "DIBackingStoreCloseRsrcFork"); FIND(get_rsrc_length, "DIBackingStoreGetRsrcForkLength");
  FIND(read_rsrc, "DIBackingStoreReadRsrcFork"); FIND(get_file_type, "DIBackingStoreGetFileType");
  return get_length && read_fork && write_fork && get_base && open_fork && close_fork && write_enabled && open_rsrc &&
         close_rsrc && get_rsrc_length && read_rsrc && get_file_type ? 0 : -ENOSYS;
}

static int data_pread(void *ctx, void *buf, size_t len, uint64_t off) {
  unsigned long done = 0;
  int e = read_fork((CBackingStore *)ctx, (long long)off, len, &done, buf);
  if (e || done != len) { TRACE("read %zu at %llu: error %d, %lu read", len, (unsigned long long)off, e, done); return -EIO; }
  return 0;
}
static int data_pwrite(void *ctx, const void *buf, size_t len, uint64_t off) {
  unsigned long done = 0;
  int e = write_fork((CBackingStore *)ctx, (long long)off, len, &done, buf);
  if (e || done != len) { TRACE("write %zu at %llu: error %d, %lu written", len, (unsigned long long)off, e, done); return -EIO; }
  return 0;
}
static int rsrc_pread(void *ctx, void *buf, size_t len, uint64_t off) {
  unsigned long done = 0;
  int e = read_rsrc((CBackingStore *)ctx, (long long)off, len, &done, buf);
  return e || done != len ? -EIO : 0;
}

typedef struct { CBackingStore *s; vimg_io data; } opened;
/* platform: the store the framework hands a format may be an encoding layer; the base store under it is the file,
 *           which is what libvimg reads. Opens the file's data fork; the caller closes it with close_fork. The io
 *           writes only when the framework opened the image writable. */
static int open_store(CBackingStore *given, opened *o) {
  long long n = 0;
  if (!given || resolve()) { TRACE("store %p: DIBackingStore functions not found", (void *)given); return -EIO; }
  CBackingStore *s = get_base(given);
  if (!s) s = given;
  int e = open_fork(s);
  if (e) { TRACE("store %p (base of %p): open error %d", (void *)s, (void *)given, e); return -EIO; }
  e = get_length(s, &n);
  if (e || n < 0) { close_fork(s); return -EIO; }
  o->s = s;
  o->data = (vimg_io){ data_pread, write_enabled(given) && write_enabled(s) ? data_pwrite : NULL, (uint64_t)n, s };
  return 0;
}

/* DiskCopy 4.2 first (its header says so outright), then NDIF (its resource fork's block map); 0 or -EINVAL */
static int open_image(const opened *o, vimg_dc42 **dc, vimg_ndif **nd) {
  long long rl = 0;
  *dc = NULL; *nd = NULL;
  int e = vimg_dc42_open(&o->data, dc);
  if (e != -EINVAL) return e;
  if (get_rsrc_length(o->s, &rl) || rl <= 0 || open_rsrc(o->s)) return -EINVAL;
  vimg_io rsrc = { rsrc_pread, NULL, (uint64_t)rl, o->s };
  vimg_io ro = o->data; ro.pwrite = NULL;
  e = vimg_ndif_open(&ro, &rsrc, nd);
  close_rsrc(o->s);
  return e;
}

static ULONG add_ref(void *self) { return (ULONG)__sync_add_and_fetch(&((instance *)self)->refs, 1); }
static ULONG release(void *self) {
  instance *in = self;
  int32_t n = __sync_sub_and_fetch(&in->refs, 1);
  if (n == 0) {
    TRACE("release: dc42 %p, ndif %p gone", (void *)in->dc, (void *)in->nd);
    vimg_dc42_close(in->dc); vimg_ndif_close(in->nd);
    if (in->open_store) close_fork(in->open_store);
    CFPlugInRemoveInstanceForFactory(in->factory); CFRelease(in->factory);
    free(in);
  }
  return (ULONG)n;
}
static HRESULT query_interface(void *self, REFIID iid, LPVOID *out) {
  CFUUIDRef want = CFUUIDCreateFromUUIDBytes(NULL, iid);
  int ok = CFEqual(want, IUnknownUUID) || CFEqual(want, DI_PLUGIN_INTERFACE);
  CFRelease(want);
  if (!ok) { *out = NULL; return E_NOINTERFACE; }
  add_ref(self); *out = self; return S_OK;
}

static int new_probe(CBackingStore *store, CFDictionaryRef options) {
  opened o; vimg_dc42 *dc; vimg_ndif *nd;
  if (open_store(store, &o)) return SCORE_DECLINE;
  int e = open_image(&o, &dc, &nd);
  vimg_dc42_close(dc); vimg_ndif_close(nd);
  close_fork(o.s);
  TRACE("newProbe: store %p (%llu bytes): %s", (void *)store, (unsigned long long)o.data.size,
        e ? "declined" : dc ? "DiskCopy 4.2, claimed" : "NDIF, claimed");
  return e ? SCORE_DECLINE : SCORE_CLAIM;
}
static int new_with_backing_store(void *self, CBackingStore *store, CFDictionaryRef options, CCFPlugInDiskImage *wrapper, CFStringRef *error) {
  instance *in = self; opened o;
  int e = open_store(store, &o);
  if (!e) {
    e = open_image(&o, &in->dc, &in->nd);
    if (e) close_fork(o.s); else { in->open_store = o.s; get_file_type(o.s, &in->file_type); }
  }
  TRACE("newWithBackingStore: store %p, error %d, %s, writable io %d", (void *)store, e,
        in->dc ? "DiskCopy 4.2" : in->nd ? "NDIF" : "nothing", e ? 0 : o.data.pwrite != NULL);
  return e ? (e == -ENOMEM ? ENOMEM : EINVAL) : 0;
}
static const char *class_name(void *self) { return "CVintageImagesDiskImage"; }
/* spec: named as 10.9's own hdiutil names them (hdiutil convert -help) */
static void names(const instance *in, CFStringRef *format, CFStringRef *description) {
  if (in->dc) { *format = CFSTR("DC42"); *description = CFSTR("Disk Copy 4.2"); }
  else if (in->file_type == TYPE_DIMG) { *format = CFSTR("RdWr"); *description = CFSTR("NDIF read/write"); }
  else if (in->nd && vimg_ndif_compressed(in->nd)) { *format = CFSTR("ROCo"); *description = CFSTR("NDIF compressed"); }
  else { *format = CFSTR("Rdxx"); *description = CFSTR("NDIF read-only"); }
}
static CFStringRef copy_format_description(void *self) { CFStringRef f, d; names(self, &f, &d); return d; }
static CFDictionaryRef copy_image_information(void *self) {
  CFStringRef f, d; names(self, &f, &d);
  const void *k[] = { CFSTR("Format"), CFSTR("Format Description") }, *v[] = { f, d };
  return CFDictionaryCreate(NULL, k, v, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}
/* spec: DiskCopy 4.2 is writable when its file is and its checksum was good at open; NDIF never, in this version */
static int is_write_enabled(void *self) {
  instance *in = self;
  int w = in->dc && vimg_dc42_writable(in->dc);
  TRACE("isWriteEnabled: %d", w);
  return w;
}
static int64_t sector_count(void *self) {
  instance *in = self;
  return in->dc ? (int64_t)vimg_dc42_sectors(in->dc) : in->nd ? (int64_t)vimg_ndif_sectors(in->nd) : 0;
}
static int read_sectors(void *self, int64_t start, int64_t count, int64_t *done, void *buf) {
  instance *in = self;
  if (done) *done = 0;
  if (start < 0 || count < 0 || (!in->dc && !in->nd)) return EINVAL;
  int e = in->dc ? vimg_dc42_read(in->dc, buf, (uint64_t)start, (uint64_t)count)
                 : vimg_ndif_read(in->nd, buf, (uint64_t)start, (uint64_t)count);
  TRACE("readSectors %lld+%lld: %d", (long long)start, (long long)count, e);
  if (e) return e == -EINVAL ? EINVAL : EIO;
  if (done) *done = count;
  return 0;
}
static int write_sectors(void *self, int64_t start, int64_t count, int64_t *done, const void *buf) {
  instance *in = self;
  if (done) *done = 0;
  if (start < 0 || count < 0) return EINVAL;
  if (!in->dc) return EROFS;
  int e = vimg_dc42_write(in->dc, buf, (uint64_t)start, (uint64_t)count);
  TRACE("writeSectors %lld+%lld: %d", (long long)start, (long long)count, e);
  if (e) return e == -EINVAL ? EINVAL : e == -EROFS ? EROFS : EIO;
  if (done) *done = count;
  return 0;
}
static int flush(void *self, unsigned int how) {
  instance *in = self;
  int e = in->dc ? vimg_dc42_flush(in->dc) : 0;
  TRACE("flush %u: %d", how, e);
  return e ? EIO : 0;
}
static DIChecksumNewWith_fn checksum_new;
static DIChecksumSetValue_fn checksum_set;
static int resolve_checksum(void) {
  FIND(checksum_new, "DIChecksumNewWith"); FIND(checksum_set, "DIChecksumSetValue");
  return checksum_new && checksum_set ? 0 : -ENOSYS;
}
static void set_sum(CChecksum *c, uint32_t v) { uint8_t be[4] = { v >> 24, v >> 16, v >> 8, v }; checksum_set(c, be); }
/* platform: verify asks for the stored checksum, then for one calculated into a new checksum of its kind; DiskCopy 4.2
 *           has its own kind (CDC42Checksum); NDIF's image checksum is left unchecked, a CNoneChecksum */
static CChecksum *copy_image_checksum(void *self) {
  instance *in = self; uint32_t stored; CChecksum *c = NULL;
  if (resolve_checksum()) return NULL;
  if (in->dc && !vimg_dc42_stored_sum(in->dc, &stored) && (c = checksum_new(DI_CHECKSUM_DC42))) set_sum(c, stored);
  /* platform: imageinfo uses this unchecked (di_interface.h, 0xb0), so a failed read still gets a CNoneChecksum */
  if (!c) c = checksum_new(DI_CHECKSUM_NONE);
  TRACE("copyImageChecksum: %p", (void *)c);
  return c;
}
static int calc_checksum(void *self, CChecksum *c) {
  instance *in = self; uint32_t stored, calculated;
  if (!in->dc || !c || resolve_checksum()) return EINVAL;
  int e = vimg_dc42_sums(in->dc, &stored, &calculated);
  TRACE("calcChecksum: %d, $%08X (stored $%08X)", e, calculated, stored);
  if (e) return e == -ENOMEM ? ENOMEM : EIO;
  set_sum(c, calculated);
  return 0;
}

static const DIPlugInInterface VTBL = {
  .QueryInterface = query_interface, .AddRef = add_ref, .Release = release,
  .newProbe = new_probe, .newWithBackingStore = new_with_backing_store,
  .getClassName = class_name, .copyFormatDescriptionString = copy_format_description,
  .copyImageInformation = copy_image_information, .isWriteEnabled = is_write_enabled,
  .getSectorCount = sector_count, .readSectors = read_sectors, .writeSectors = write_sectors, .flush = flush,
  .copyImageChecksum = copy_image_checksum, .calcChecksum = calc_checksum,
};

/* the one exported symbol: Info.plist's CFPlugInFactories names it */
__attribute__((visibility("default"))) void *VintageImagesFactory(CFAllocatorRef allocator, CFUUIDRef type) {
  if (!CFEqual(type, DI_PLUGIN_TYPE)) return NULL;
  instance *in = calloc(1, sizeof *in);
  if (!in) return NULL;
  in->vtbl = &VTBL; in->refs = 1;
  in->factory = CFUUIDCreateFromString(NULL, CFSTR(VF_FACTORY_UUID));
  CFPlugInAddInstanceForFactory(in->factory);
  return in;
}
