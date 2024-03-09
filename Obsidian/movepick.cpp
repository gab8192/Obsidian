#include "movepick.h"
#include "tuning.h"
#include "uci.h"

MovePicker::MovePicker(
  SearchType _searchType, Position& _pos,
  Move _ttMove, Move _killerMove, Move _counterMove,
  MainHistory& _mainHist, CaptureHistory& _capHist,
  int _seeMargin,
  Search::SearchInfo* _ss) :
  searchType(_searchType), pos(_pos),
  ttMove(_ttMove),
  mainHist(_mainHist), capHist(_capHist), 
  seeMargin(_seeMargin),
  ss(_ss)
{
  this->stage = pos.isPseudoLegal(ttMove) ? PLAY_TT_MOVE : GEN_CAPTURES;

  // Ensure tt, killer, and counter, are all different

  if (_killerMove == _ttMove)
    this->killerMove = MOVE_NONE;
  else
    this->killerMove = _killerMove;

  if (_counterMove == _ttMove || _counterMove == _killerMove)
    this->counterMove = MOVE_NONE;
  else
    this->counterMove = _counterMove;
}

int pieceTo(Position& pos, Move m) {
  return pos.board[move_from(m)] * SQUARE_NB + move_to(m);
}

Move_Score nextMove0(MoveList& moveList, const int visitedCount) {
  int bestMoveI = visitedCount;
  int bestMoveScore = moveList[bestMoveI].score;

  const int size = moveList.size();
  for (int i = visitedCount + 1; i < size; i++) {
    int thisScore = moveList[i].score;
    if (thisScore > bestMoveScore) {
      bestMoveScore = thisScore;
      bestMoveI = i;
    }
  }

  Move_Score result = moveList[bestMoveI];
  moveList[bestMoveI] = moveList[visitedCount];
  return result;
}

void MovePicker::scoreQuiets() {

  int i = 0;
  while (i < quiets.size()) {
    Move move = quiets[i].move;

    if (move == ttMove || move == killerMove || move == counterMove) {
      quiets.remove(i);
      continue;
    }

    int chIndex = pieceTo(pos, move);

    quiets[i++].score =
      mainHist[pos.sideToMove][move_from_to(move)]
      + (ss - 1)->contHistory[chIndex]
      + (ss - 2)->contHistory[chIndex]
      + (ss - 4)->contHistory[chIndex]/2;
  }
}

void MovePicker::scoreCaptures() {
  int i = 0;
  while (i < captures.size()) {

    Move move = captures[i].move;

    if (move == ttMove) {
      captures.remove(i);
      continue;
    }

    int& moveScore = captures[i].score;

    // initial score
    moveScore = 0;

    MoveType mt = move_type(move);
    PieceType captured = piece_type(pos.board[move_to(move)]);

    moveScore += PIECE_VALUE[mt == MT_EN_PASSANT ? PAWN : captured] * 128;

    if (mt == MT_PROMOTION)
      moveScore += PIECE_VALUE[promo_type(move)] * 128;
    else {
      moveScore += capHist[pieceTo(pos, move)][captured];
    }

    i++;
  }
}

Move MovePicker::nextMove(Stage* outStage) {
  select:
  switch (stage)
  {
  case PLAY_TT_MOVE:
  {
    ++stage;
    *outStage = PLAY_TT_MOVE;
    return ttMove;
  }
  case GEN_CAPTURES: 
  {
    getStageMoves(pos, ADD_CAPTURES, &captures);
    scoreCaptures();
    ++stage;
    [[fallthrough]];
  }
  case PLAY_GOOD_CAPTURES:
  {
    nextGoodCap:
    if (capIndex < captures.size()) {
      Move_Score move = nextMove0(captures, capIndex++);
      if (pos.seeGe(move.move, seeMargin)) { // good capture
        *outStage = PLAY_GOOD_CAPTURES;
        return move.move;
      } else {
        badCaptures.add(move);
        goto nextGoodCap;
      }
    }

    if (  (searchType == QSEARCH && !pos.checkers)
        || searchType == PROBCUT)
      return MOVE_NONE;

    ++stage;
    [[fallthrough]];
  }
  case PLAY_KILLER:
  {
    ++stage;
    if (pos.isQuiet(killerMove) && pos.isPseudoLegal(killerMove)) {
      *outStage = PLAY_KILLER;
      return killerMove;
    }
    [[fallthrough]];
  }
  case PLAY_COUNTER:
  {
    ++stage;
    if (pos.isQuiet(counterMove) && pos.isPseudoLegal(counterMove)) {
      *outStage = PLAY_COUNTER;
      return counterMove;
    }
    [[fallthrough]];
  }
  case GEN_QUIETS: 
  {
    getStageMoves(pos, ADD_QUIETS, &quiets);
    scoreQuiets();

    ++stage;
    [[fallthrough]];
  }
  case PLAY_QUIETS: 
  {
    if (quietIndex < quiets.size()) {
      Move_Score move = nextMove0(quiets, quietIndex++);
      *outStage = PLAY_QUIETS;
      return move.move;
    }

    ++stage;
    [[fallthrough]];
  }
  case PLAY_BAD_CAPTURES:
  {
    // If any captures are left, they are all bad
    if (badCapIndex < badCaptures.size()) {
      Move_Score move = nextMove0(badCaptures, badCapIndex++);
      *outStage = PLAY_BAD_CAPTURES;
      return move.move;
    }
  }
  }

  return MOVE_NONE;
}