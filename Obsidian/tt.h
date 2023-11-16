#pragma once

#include "position.h"


namespace TT {

  enum Flag : uint8_t {
    NO_FLAG = 0,

    FLAG_LOWER = 1,
    FLAG_UPPER = 2,
    FLAG_EXACT = FLAG_LOWER | FLAG_UPPER
  };

#pragma pack(1)
  struct Entry {

    inline void store(Key _key, Flag _flag, int _depth, Move _move, Score _value, Score _eval, bool isPV) {
      if (abs(_value) >= TB_WIN_IN_MAX_PLY)
        return;

      if (!matches(_key)
        || _depth + 4 + 2*isPV >= this->depth
        || _flag == FLAG_EXACT) {

        if (!matches(_key) || _move)
          this->move = _move;

        this->key32 = uint32_t(_key);
        this->flag = _flag;
        this->depth = _depth;
        this->value = _value;
        this->staticEval = _eval;
      }
    }

    inline bool matches(Key key) const {
      return this->key32 == uint32_t(key);
    }

    inline Score getStaticEval() const {
      return Score(staticEval);
    }

    inline int getDepth() const {
      return depth;
    }
    inline Flag getFlag() const {
      return flag;
    }
    inline Move getMove() const {
      return Move(move);
    }
    inline Score getScore() const {
      return Score(value);
    }

    inline void clear() {
      key32 = 0xcafe;
      depth = -1;
      flag = NO_FLAG;
      move = MOVE_NONE;
      value = SCORE_NONE;
      staticEval = SCORE_NONE;
    }

  private:
    uint32_t key32;
    int16_t staticEval;
    Flag flag;
    uint8_t depth;
    int16_t move;
    int16_t value;
  };
#pragma pack()

  // Initialize/clear the TT
  void clear();

  void resize(size_t megaBytes);

  void prefetch(Key key);

  Entry* probe(Key key, bool& hit);
}