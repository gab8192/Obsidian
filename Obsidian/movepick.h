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

enum SearchType {
  PVS, QSEARCH, PROBCUT
};

ENABLE_INCR_OPERATORS_ON(MpStage);

class MovePicker {
public:

  MpStage stage;

  Search::SearchThread* thread = nullptr;
  
  MovePicker(
    SearchType _searchType, Position& _pos,
    Move _ttMove, Move _killerMove, Move _counterMove,
    MainHistory& _mainHist, CaptureHistory& _capHist,
    int _seeMargin,
    Search::SearchInfo* _ss);

  Move nextMove(MpStage* outStage);

private:
  SearchType searchType;
  Position& pos;

  Move ttMove;
  Move killerMove;
  Move counterMove;

  MainHistory& mainHist;
  CaptureHistory& capHist;
  
  int seeMargin;
  
  Search::SearchInfo* ss;

  MoveList captures;
  MoveList quiets;

  int capIndex = 0, quietIndex = 0;

  void scoreCaptures();

  void scoreQuiets();
};