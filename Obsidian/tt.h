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

    inline void store(Key _key, Flag _flag, int _depth, Move _move, Score _score, Score _eval, bool isPV, int ply) {

      if (   _flag == FLAG_EXACT
          || !matches(_key)
          || _depth + 4 + 2*isPV > this->depth) {

        if (_score >= TB_WIN_IN_MAX_PLY)
          _score += ply;
        else if (score <= TB_LOSS_IN_MAX_PLY)
          _score -= ply;

        if (!matches(_key) || _move)
          this->move = _move;

        this->key64 = _key;
        this->flag = _flag;
        this->depth = _depth;
        this->score = _score;
        this->staticEval = _eval;
      }

    }

    inline bool matches(Key key) const {
      return this->key64 == key;
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
    inline Score getScore(int ply) const {
      if (score == SCORE_NONE)
        return SCORE_NONE;

      if (score >= TB_WIN_IN_MAX_PLY)
        return Score( score - ply );
      if (score <= TB_LOSS_IN_MAX_PLY)
        return Score(score + ply);

      return Score(score);
    }

    inline void clear() {
      key64 = 0xcafe;
      depth = -1;
      flag = NO_FLAG;
      move = MOVE_NONE;
      score = SCORE_NONE;
      staticEval = SCORE_NONE;
    }

  private:
    Key key64;
    int16_t staticEval;
    Flag flag;
    uint8_t depth;
    uint16_t move;
    int16_t score;
  };
#pragma pack()

  // Initialize/clear the TT
  void clear();

  void resize(size_t megaBytes);

  void prefetch(Key key);

  Entry* probe(Key key, bool& hit);
}