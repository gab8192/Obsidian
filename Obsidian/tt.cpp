#include "tt.h"

#include <iostream>

namespace TT {

  constexpr uint8_t MAX_AGE = 1 << 5;

  uint8_t tableAge;
  Entry* entries = nullptr;
  uint64_t entryCount;

  void clear() {
    memset(entries, 0, sizeof(Entry) * entryCount);
    tableAge = 1;
  }

  void nextSearch() {
    tableAge = (tableAge+1) % MAX_AGE;
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
    if (hit = entry->matches(key))
      entry->updateAge();
    return entry;
  }

  void Entry::updateAge() {
    agePvBound = (agePvBound & (FLAG_PV | FLAG_EXACT)) | (tableAge << 3);
  }

  void Entry::store(Key _key, Flag _bound, int _depth, Move _move, Score _score, Score _eval, bool isPV, int ply) {
    if ( _bound == FLAG_EXACT
      || !matches(_key)
      || _depth + 4 + 2*isPV > this->depth
      || getAge() != tableAge) {

      if (_score >= SCORE_TB_WIN_IN_MAX_PLY)
        _score += ply;
      else if (score <= SCORE_TB_LOSS_IN_MAX_PLY)
        _score -= ply;

      if (!matches(_key) || _move)
        this->move = _move;

      this->key32 = (uint32_t) _key;
      this->depth = _depth;
      this->score = _score;
      this->staticEval = _eval;
      this->agePvBound = _bound | (isPV << 2) | (tableAge << 3);
    }
  }
}