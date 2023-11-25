#pragma once

#include "history.h"
#include "movegen.h"

enum MpStage {
  TT_MOVE,
  GEN_CAPTURES,
  GOOD_CAPTURES,
  GEN_QUIETS,
  KILLER,
  COUNTER,
  QUIETS,
  BAD_CAPTURES,

};

ENABLE_INCR_OPERATORS_ON(MpStage);

class MovePicker {
public:
  
  MovePicker(
    Position& _pos,
    Move _ttMove, Move _killerMove, Move _counterMove,
    MainHistory* _mainHist, CaptureHistory* _capHist,
    PieceToHistory* _ch1, PieceToHistory* _ch2, PieceToHistory* _ch4);

  Move nextMove(MpStage* outStage);

private:
  Position& pos;

  Move ttMove;
  Move killerMove;
  Move counterMove;

  bool killerFound, counterFound;

  MainHistory& mainHist;
  CaptureHistory& capHist;
  
  PieceToHistory& contHist1;
  PieceToHistory& contHist2;
  PieceToHistory& contHist4;

  MpStage stage;

  MoveList captures;
  MoveList quiets;

  int capIndex, quietIndex;

  void scoreCaptures();

  void scoreQuiets();
};