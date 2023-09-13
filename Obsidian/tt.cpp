#include "tt.h"

#include <iostream>

using namespace std;

namespace TT {

  Entry* transTable = nullptr;
  uint64_t entryCount;

  void clear() {
	for (uint64_t i = 0; i < entryCount; i++)
	  transTable[i].clear();
  }

  void resize(size_t megaBytes) {
	size_t bytes = megaBytes * 1024ULL * 1024ULL;
	entryCount = bytes / sizeof(Entry);

	if (transTable != nullptr)
	  free(transTable);

	transTable = (Entry*) malloc(bytes);
	clear();
  }

  Entry* probe(Position& pos) {
	Key key = pos.key;

	Entry* entry = &transTable[key % entryCount];
	if (entry->getKey() != key) {
	  entry->clear();
	  entry->storeKey(key);
	}
	return entry;
  }
}