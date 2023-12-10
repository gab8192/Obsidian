#pragma once

#include "history.h"
#include "movegen.h"
#include "search.h"

enum MpStage {
  TT_MOVE,
  GEN_CAPTURES,
  GOOD_CAPTURES,
  KILLER,
  COUNTER,
  GEN_QUIETS,
  QUIETS,
  BAD_CAPTURES,

};

ENABLE_INCR_OPERATORS_ON(MpStage);

class MovePicker {
public:
  
  MovePicker(
    bool _isQsearch, Position& _pos,
    Move _ttMove, Move _killerMove, Move _counterMove,
    Search::SearchThread* _thread,
    Search::SearchInfo* _ss);

  void skipQuiets();

  Move nextMove(MpStage* outStage);

private:
  bool isQsearch;
  Position& pos;

  Move ttMove;
  Move killerMove;
  Move counterMove;

  Search::SearchThread* thread;
  
  Search::SearchInfo* ss;

  MpStage stage;

  MoveList captures;
  MoveList quiets;

  int capIndex, quietIndex;

  void scoreCaptures();

  void scoreQuiets();
};