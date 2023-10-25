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

    inline void store(Key _key, Flag _flag, int _depth, Move _move, Value _value, Value _eval) {
      if (abs(_value) >= VALUE_TB_WIN_IN_MAX_PLY)
        return;

      if (!matches(_key)
        || _depth >= this->depth) {

        if (!matches(_key) || _move)
          this->move = _move;

        this->keyHi32 = (_key >> 32);
        this->flag = _flag;
        this->depth = _depth;
        this->value = _value;
        this->staticEval = _eval;
      }
    }

    inline bool matches(Key key) const {
      return this->keyHi32 == (key >> 32);
    }

    inline Value getStaticEval() const {
      return Value(staticEval);
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
    inline Value getValue() const {
      return Value(value);
    }

    inline void clear() {
      keyHi32 = 0xcafe;
      depth = -1;
      flag = NO_FLAG;
      move = MOVE_NONE;
      value = VALUE_NONE;
      staticEval = VALUE_NONE;
    }

  private:
    uint32_t keyHi32;
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

  Entry* probe(Key key, bool& hit);
}