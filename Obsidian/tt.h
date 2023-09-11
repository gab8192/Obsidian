#pragma once

#include "position.h"


namespace TT {

  enum Flag : uint8_t {
    FLAG_ABSENT, FLAG_BETA, FLAG_ALPHA, FLAG_EXACT
  };

#pragma pack(1)
  struct Entry {

    inline void store(Flag _flag, int _depth, Move _move, Value _value) {
      if (abs(_value) >= VALUE_TB_WIN_IN_MAX_PLY)
        return;

      if (_depth >= this->depth) {

        this->flag = _flag;
        this->depth = _depth;
        this->move = _move;
        this->value = _value;
      }
    }

    inline Key getKey() {
      return key;
    }

    inline void storeKey(Key _key) {
      this->key = _key;
    }

    inline Value getStaticEval() {
      return Value(staticEval);
    }

    inline void storeStaticEval(Value _staticEval) {
      this->staticEval = _staticEval;
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
      depth = 0;
      flag = FLAG_ABSENT;
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

  Entry* probe(Position& pos);
}