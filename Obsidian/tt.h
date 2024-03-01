#pragma once

#include "position.h"


namespace TT {

  using Flag = uint8_t;

  constexpr Flag 
    NO_FLAG = 0,
    FLAG_LOWER = 1,
    FLAG_UPPER = 2,
    FLAG_EXACT = FLAG_LOWER | FLAG_UPPER,
    FLAG_PV = 4;

  constexpr int EntriesPerBucket = 3;

  struct Entry {

    void store(Key _key, Flag _bound, int _depth, Move _move, Score _score, Score _eval, bool isPV, int ply);

    inline bool matches(Key key) const {
      return this->key16 == (uint16_t) key;
    }

    inline Score getStaticEval() const {
      return staticEval;
    }

    inline int getDepth() const {
      return depth;
    }

    inline Flag getBound() const {
      return flags & FLAG_EXACT;
    }

    inline Move getMove() const {
      return Move(move);
    }

    inline Score getScore(int ply) const {
      if (score == SCORE_NONE)
        return SCORE_NONE;

      if (score >= SCORE_TB_WIN_IN_MAX_PLY)
        return score - ply;
      if (score <= SCORE_TB_LOSS_IN_MAX_PLY)
        return score + ply;

      return score;
    }

    inline bool wasPV() const {
      return flags & FLAG_PV;
    }

    inline bool isEmpty() const {
      return flags == NO_FLAG;
    }

    inline int quality() const {
      return this->depth + 8 * (getBound() == FLAG_EXACT);
    }

  private:
    uint16_t key16;
    int16_t staticEval;
    Flag flags;
    uint8_t depth;
    uint16_t move;
    int16_t score;
  };

  struct Bucket {
    Entry entries[EntriesPerBucket];
    int16_t padding;
  };

  // Initialize/clear the TT
  void clear();

  void resize(size_t megaBytes);

  void prefetch(Key key);

  Entry* probe(Key key, bool& hit);
}