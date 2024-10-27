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
    PLAY_TT,
    GEN_CAPTURES,
    PLAY_GOOD_CAPTURES,
    PLAY_KILLER,
    PLAY_COUNTER,
    GEN_QUIETS,
    PLAY_QUIETS,
    PLAY_BAD_CAPTURES,

    QS_PLAY_TT,
    QS_GEN_CAPTURES,
    QS_PLAY_CAPTURES,
    QS_GEN_QUIET_CHECKS,
    QS_PLAY_QUIET_CHECKS,

    IN_CHECK_PLAY_TT,
    IN_CHECK_GEN_CAPTURES,
    IN_CHECK_PLAY_CAPTURES,
    IN_CHECK_GEN_QUIETS,
    IN_CHECK_PLAY_QUIETS
  };

  // Constructor for pvs and probcut
  MovePicker(
    SearchType _searchType, Position& _pos,
    Move _ttMove, Move _killerMove, Move _counterMove,
    MainHistory& _mainHist, CaptureHistory& _capHist,
    int _seeMargin,
    Search::SearchInfo* _ss);

  Move nextMove(bool skipQuiets);

  bool genQuietChecks = false;

private:
  SearchType searchType;
  Position& pos;

  Move ttMove = MOVE_NONE;
  Move killerMove = MOVE_NONE;
  Move counterMove = MOVE_NONE;

  MainHistory& mainHist;
  CaptureHistory& capHist;

  int seeMargin;

  Search::SearchInfo* ss;

  Stage stage;

  MoveList captures;
  MoveList quiets;
  MoveList badCaptures;

  int capIndex = 0, quietIndex = 0, badCapIndex = 0;

  void scoreCaptures();

  void scoreQuiets();
};

ENABLE_INCR_OPERATORS_ON(MovePicker::Stage);
