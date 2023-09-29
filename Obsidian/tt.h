#pragma once

#include "position.h"


namespace TT {

  enum Flag : uint8_t {
    FLAG_LOWER = 1,
    FLAG_UPPER = 2,
    FLAG_EXACT = 3
  };

#pragma pack(1)
  struct Entry {

    inline void store(Key _key, Flag _flag, int _depth, Move _move, Value _value, Value _eval) {
      if (abs(_value) >= VALUE_TB_WIN_IN_MAX_PLY)
        return;

      if (_key != this->key
        || _depth >= this->depth) {

        if (_key != this->key || _move || !this->move)
          this->move = _move;

        this->key = _key;
        this->flag = _flag;
        this->depth = _depth;
        this->value = _value;
        this->staticEval = _eval;
      }
    }

    inline Key getKey() {
      return key;
    }

    inline Value getStaticEval() {
      return Value(staticEval);
    }

    inline int getDepth() {
      return depth;
    }
    inline Flag getFlag() {
      return flag;
    }
    inline Move getMove() {
      return Move(move);
    }
    inline Value getValue() {
      return Value(value);
    }

    inline void clear() {
      key = 0xcafe;
      depth = -1;
      flag = (Flag)0;
      move = MOVE_NONE;
      value = VALUE_NONE;
      staticEval = VALUE_NONE;
    }

  private:
    Key key;
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