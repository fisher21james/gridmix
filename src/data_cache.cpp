#include "data_cache.h"
#include <Preferences.h>
#include <string.h>
#include <type_traits>

static_assert(std::is_trivially_copyable<CachedData>::value, "CachedData is saved as raw bytes");

static const char *NS = "gridmix";
static const char *KEY = "cache";
static const uint32_t MAGIC = 0x474D4331;   // "GMC1"; change if CachedData's layout changes

struct Blob {
  uint32_t magic;
  uint32_t size;
  char feed_key[24];
  CachedData data;
};

bool cache_load(const char *feed_key, CachedData &out) {
  Preferences p;
  if (!p.begin(NS, true)) return false;
  Blob *b = new Blob;
  const size_t n = p.getBytes(KEY, b, sizeof(Blob));
  p.end();
  const bool ok = n == sizeof(Blob) && b->magic == MAGIC && b->size == sizeof(CachedData) &&
                  !strncmp(b->feed_key, feed_key, sizeof b->feed_key) && b->data.snap.valid;
  if (ok) out = b->data;
  delete b;
  return ok;
}

void cache_save(const char *feed_key, const CachedData &in) {
  Blob *b = new Blob;
  memset(b, 0, sizeof(Blob));
  b->magic = MAGIC;
  b->size = sizeof(CachedData);
  strncpy(b->feed_key, feed_key, sizeof b->feed_key - 1);
  b->data = in;
  Preferences p;
  if (p.begin(NS, false)) {
    p.putBytes(KEY, b, sizeof(Blob));
    p.end();
  }
  delete b;
}

void cache_clear() {
  Preferences p;
  if (p.begin(NS, false)) {
    p.remove(KEY);
    p.end();
  }
}
