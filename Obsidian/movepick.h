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

  MpStage stage;
  
  MovePicker(
    bool _isQsearch, Position& _pos,
    Move _ttMove, Move _killerMove, Move _counterMove,
    MainHistory& _mainHist, CaptureHistory& _capHist,
    Search::SearchInfo* _ss);

  Move nextMove(MpStage* outStage);

private:
  bool isQsearch;
  Position& pos;

  Move ttMove;
  Move killerMove;
  Move counterMove;

  MainHistory& mainHist;
  CaptureHistory& capHist;
  
  Search::SearchInfo* ss;

  MoveList captures;
  MoveList quiets;

  int capIndex, quietIndex;

  void scoreCaptures();

  void scoreQuiets();
};