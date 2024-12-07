#include "tt.h"
#include "uci.h"
#include "util.h"

#include <vector>

namespace TT {

  constexpr size_t MEGA = 1024 * 1024;
  constexpr uint8_t MAX_AGE = 1 << 5;

  uint8_t tableAge;
  Bucket* buckets = nullptr;
  uint64_t bucketCount;

  void clear() {
    tableAge = 0;

    const int threadCount = UCI::Options["Threads"];
    const uint64_t chunkSize = bucketCount / threadCount;

    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (int i = 0; i < threadCount; ++i)
    {
      threads.emplace_back([chunkSize, i]
      {
        memset(&buckets[chunkSize * i], 0, chunkSize * sizeof(Bucket));
      });
    }

    const uint64_t cleared = threadCount * chunkSize;
    if (cleared < bucketCount)
      memset(&buckets[cleared], 0, (bucketCount - cleared) * sizeof(Bucket));

    for (auto &thread : threads)
      thread.join();
  }

  void nextSearch() {
    tableAge = (tableAge+1) % MAX_AGE;
  }

  void resize(size_t megaBytes) {
    if (buckets)
      Util::freeAlign(buckets);

    size_t bytes = megaBytes * MEGA;
    bucketCount = bytes / sizeof(Bucket);

    buckets = (Bucket*) Util::allocAlign(bytes);
    
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

  int qualityOf(Entry* e) {
    return e->getDepth() - 8 * e->getAgeDistance();
  }

  Entry* probe(Key key, bool& hit) {

    Entry* entries = getBucket(key)->entries;

    for (int i = 0; i < EntriesPerBucket; i++) {
      if (entries[i].matches(key)) {
        hit = ! entries[i].isEmpty();
        return & entries[i];
      }
    }

    Entry* worstEntry = & entries[0];

    for (int i = 1; i < EntriesPerBucket; i++) {
      if (qualityOf(& entries[i]) < qualityOf(worstEntry))
        worstEntry = & entries[i];
    }

    hit = false;
    return worstEntry;
  }

  int hashfull() {
    int entryCount = 0;
    for (int i = 0; i < 1000; i++) {
      for (int j = 0; j < EntriesPerBucket; j++) {
        Entry* entry = & buckets[i].entries[j];
        if (entry->getAge() == tableAge && !entry->isEmpty())
          entryCount++;
      }
    }
    return entryCount / EntriesPerBucket;
  }

  int Entry::getAgeDistance() {
    return (MAX_AGE + tableAge - getAge()) % MAX_AGE;
  }

  void Entry::store(Key _key, Flag _bound, int _depth, Move _move, Score _score, Score _eval, bool isPV, int ply) {

     if (!matches(_key) || _move)
        this->move = _move;

    if (_score != SCORE_NONE) {
      if (_score >= SCORE_TB_WIN_IN_MAX_PLY)
        _score += ply;
      else if (score <= SCORE_TB_LOSS_IN_MAX_PLY)
        _score -= ply;
    }

    if ( _bound == FLAG_EXACT
      || !matches(_key)
      || getAgeDistance()
      || _depth + 4 + 2*isPV > this->depth) {

        this->key16 = (uint16_t) _key;
        this->depth = _depth;
        this->score = _score;
        this->staticEval = _eval;
        this->agePvBound = _bound | (isPV << 2) | (tableAge << 3);
      }
  }
}
