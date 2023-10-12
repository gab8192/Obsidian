#include "tt.h"

#include <iostream>

using namespace std;

namespace TT {

  Entry* entries = nullptr;
  uint64_t entryCount;

  void clear() {
    for (uint64_t i = 0; i < entryCount; i++)
      entries[i].clear();
  }

  void resize(size_t megaBytes) {
    size_t bytes = megaBytes * 1024ULL * 1024ULL;
    entryCount = bytes / sizeof(Entry);

    if (entries != nullptr)
      delete[] entries;

    entries = new Entry[entryCount];
    clear();
  }

  Entry* probe(Key key, bool& hit) {
    Entry* entry = &entries[key % entryCount];
    hit = entry->matches(key);
    return entry;
  }
}