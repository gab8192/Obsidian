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

constexpr int promotionScores[] = {
    0, 0, 200000, -300000, -200000, 300000
};

int nextMoveIndex(MoveList& moveList, int scannedMoves) {
  int bestMoveI = scannedMoves;

  int bestMoveScore = moveList[bestMoveI].score;

  int size = moveList.size();
  for (int i = scannedMoves + 1; i < size; i++) {
    int thisScore = moveList[i].score;
    if (thisScore > bestMoveScore) {
      bestMoveScore = thisScore;
      bestMoveI = i;
    }
  }

  return bestMoveI;
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
      moveScore += promotionScores[promo_type(move)];
    else {
      if (pos.seeGe(move, seeMargin))
        moveScore += 500000;
      else
        moveScore -= 500000;
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
    if (capIndex < captures.size()) {
      int moveI = nextMoveIndex(captures, capIndex);
      Move_Score move = captures[moveI];
      if (move.score > 0) { // good capture
        captures[moveI] = captures[capIndex++];
        *outStage = PLAY_GOOD_CAPTURES;
        return move.move;
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
      int moveI = nextMoveIndex(quiets, quietIndex);
      Move_Score move = quiets[moveI];
      quiets[moveI] = quiets[quietIndex++];
      *outStage = PLAY_QUIETS;
      return move.move;
    }

    ++stage;
    [[fallthrough]];
  }
  case PLAY_BAD_CAPTURES:
  {
    // If any captures are left, they are all bad
    if (capIndex < captures.size()) {
      int moveI = nextMoveIndex(captures, capIndex);
      Move_Score move = captures[moveI];
      captures[moveI] = captures[capIndex++];
      *outStage = PLAY_BAD_CAPTURES;
      return move.move;
    }
  }
  }

  return MOVE_NONE;
}