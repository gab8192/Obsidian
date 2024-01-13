#include "tt.h"

#include <iostream>

namespace TT {

  Entry* entries = nullptr;
  uint64_t entryCount;

  void clear() {
    memset(entries, 0, sizeof(Entry) * entryCount);
  }

  void resize(size_t megaBytes) {
    size_t bytes = megaBytes * 1024ULL * 1024ULL;
    entryCount = bytes / sizeof(Entry);

    if (entries != nullptr)
      delete[] entries;

    entries = new Entry[entryCount];
    clear();
  }

  uint64_t index(Key key) {
    using uint128 = unsigned __int128;
    return (uint128(key) * uint128(entryCount)) >> 64;
  }

  void prefetch(Key key) {
#if defined(_MSC_VER)
    _mm_prefetch((char*)&entries[index(key)], _MM_HINT_T0);
#else
    __builtin_prefetch(&entries[index(key)]);
#endif
  }

  Entry* probe(Key key, bool& hit) {
    Entry* entry = &entries[index(key)];
    hit = entry->matches(key);
    return entry;
  }
}