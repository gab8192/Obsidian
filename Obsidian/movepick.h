#pragma once

#include "history.h"
#include "movegen.h"
#include "search.h"

class MovePicker {
public:

  enum SearchType {
    PVS, QSEARCH, PROBCUT
  };

  enum Stage {
    PLAY_TT_MOVE,
    GEN_CAPTURES,
    PLAY_GOOD_CAPTURES,
    PLAY_KILLER,
    PLAY_COUNTER,
    GEN_QUIETS,
    PLAY_QUIETS,
    PLAY_BAD_CAPTURES,

    IN_CHECK_TT_MOVE,
    IN_CHECK_GEN_CAPTURES,
    IN_CHECK_PLAY_CAPTURES,
    IN_CHECK_GEN_QUIETS,
    IN_CHECK_PLAY_QUIETS
  };

  Stage stage;
  
  MovePicker(
    SearchType _searchType, Position& _pos,
    Move _ttMove, Move _killerMove, Move _counterMove,
    MainHistory& _mainHist, CaptureHistory& _capHist,
    int _seeMargin,
    Search::SearchInfo* _ss);

  Move nextMove(bool skipQuiets, Stage* outStage);

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
  MoveList badCaptures;

  int capIndex = 0, quietIndex = 0, badCapIndex = 0;

  void scoreCaptures();

  void scoreQuiets();
};

ENABLE_INCR_OPERATORS_ON(MovePicker::Stage);