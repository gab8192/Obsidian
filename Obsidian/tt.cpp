#include "tt.h"

#include <iostream>

using namespace std;

namespace TT {

  Cluster* clusters = nullptr;
  uint64_t clusterCount;

  void clear() {
    memset(clusters, 0, sizeof(Cluster)*clusterCount);
  }

  void resize(size_t megaBytes) {
    size_t bytes = megaBytes * 1024ULL * 1024ULL;
    clusterCount = bytes / sizeof(Cluster);

    if (clusters != nullptr)
      delete[] clusters;

    clusters = new Cluster[clusterCount];
    clear();
  }

  uint64_t index(Key key) {
    using uint128 = unsigned __int128;
    return (uint128(key) * uint128(clusterCount)) >> 64;
  }

  void prefetch(Key key) {
    __builtin_prefetch(&clusters[index(key)]);
  }

  Entry* probe(Key key, bool& hit) {
    
    Entry* entries = clusters[index(key)].entries;

    for (int i = 0; i < ClusterSize; i++) {
      if (entries[i].matches(key)) {
        hit = true;
        return & entries[i];
      }
    }
    
    // When none of the entries in the cluster match the wanted key, we must decide which
    // entry to return. The entry that we return, will usually be overwritten by search.
    // So it is smart to return the entry that is worth the least

    Entry* worstEntry = & entries[0];
    int worstRelevance = worstEntry->getRelevance();

    for (int i = 1; i < ClusterSize; i++) {
      int thisRelevance = entries[i].getRelevance();
      if (thisRelevance < worstRelevance) {
        worstEntry = &entries[i];
        worstRelevance = thisRelevance;
      }
    }

    hit = false;
    return worstEntry;
  }
}