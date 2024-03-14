#include "tt.h"

#include <iostream>

namespace TT {

  constexpr uint8_t MAX_AGE = 1 << 5;

  uint8_t tableAge;
  Bucket* buckets = nullptr;
  uint64_t bucketCount;

  void clear() {
    memset(buckets, 0, sizeof(Bucket) * bucketCount);
    tableAge = 1;
  }

  void nextSearch() {
    tableAge = (tableAge+1) % MAX_AGE;
  }

  void resize(size_t megaBytes) {
    size_t bytes = megaBytes * 1024ULL * 1024ULL;
    bucketCount = bytes / sizeof(Bucket);

    if (buckets != nullptr)
      delete[] buckets;

    buckets = new Bucket[bucketCount];
    clear();
  }

  Bucket* getBucket(Key key) {
    using uint128 = unsigned __int128;
    uint64_t index = (uint128(key) * uint128(bucketCount)) >> 64;
    return & buckets[index];
  }

  void prefetch(Key key) {
    __builtin_prefetch(getBucket(key));
  }

  Entry* probe(Key key, bool& hit) {

    Entry* entries = getBucket(key)->entries;

    for (int i = 0; i < EntriesPerBucket; i++) {
      if (entries[i].matches(key) || entries[i].isEmpty()) {
        hit = ! entries[i].isEmpty();
        entries[i].updateAge();
        return & entries[i];
      }
    }

    Entry* worstEntry = & entries[0];

    for (int i = 1; i < EntriesPerBucket; i++) {
      if (entries[i].getQuality() < worstEntry->getQuality())
        worstEntry = & entries[i];
    }
    
    hit = false;
    return worstEntry;
  }

  void Entry::store(Key _key, Flag _bound, int _depth, Move _move, Score _score, Score _eval, bool isPV, int ply) {

     if (!matches(_key) || _move)
          this->move = _move;
    
    if ( _bound == FLAG_EXACT
      || !matches(_key)
      || _depth + 4 + 2*isPV > this->depth) {

        if (_score >= SCORE_TB_WIN_IN_MAX_PLY)
          _score += ply;
        else if (score <= SCORE_TB_LOSS_IN_MAX_PLY)
          _score -= ply;

        this->key16 = (uint16_t) _key;
        this->depth = _depth;
        this->score = _score;
        this->staticEval = _eval;
        this->agePvBound = _bound | (isPV << 2) | (tableAge << 3);
      }
  }

  void Entry::updateAge() {
    agePvBound = (agePvBound & (FLAG_EXACT | FLAG_PV)) | tableAge;
  }

  int Entry::getQuality() {
    int ageDistance = (MAX_AGE + tableAge - getAge()) % MAX_AGE;
    return depth - 8 * ageDistance;
  }
}