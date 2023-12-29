#include "tt.h"

#include <iostream>

#if defined(__linux__)
#include <sys/mman.h>
#endif

using namespace std;

namespace TT {

  constexpr size_t Mega = 1024 * 1024;

  Entry* entries = nullptr;
  uint64_t entryCount;

  void clear() {
    for (uint64_t i = 0; i < entryCount; i++)
      entries[i].clear();
  }

  void resize(size_t megaBytes) {

    if (entries)
      free(entries);

    size_t size = megaBytes * Mega;
    entryCount = size / sizeof(Entry);

#if defined(__linux__)
    entries = (Entry*) aligned_alloc(2 * Mega, size);
    madvise(entries, size, MADV_HUGEPAGE);
#else
    entries = (Entry*) malloc(size);
    exit(-1);
#endif 

    clear();
  }

  uint64_t index(Key key) {
    using uint128 = unsigned __int128;
    return (uint128(key) * uint128(entryCount)) >> 64;
  }

  void prefetch(Key key) {
    __builtin_prefetch(&entries[index(key)]);
  }

  Entry* probe(Key key, bool& hit) {
    Entry* entry = &entries[index(key)];
    hit = entry->matches(key);
    return entry;
  }
}