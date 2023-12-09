#include "movepick.h"
#include "uci.h"

MovePicker::MovePicker(
  bool _isQsearch, Position& _pos,
  Move _ttMove, Move _killerMove, Move _counterMove,
  MainHistory& _mainHist, CaptureHistory& _capHist,
  Search::SearchInfo* _ss) :
  isQsearch(_isQsearch), pos(_pos),
  ttMove(_ttMove),
  mainHist(_mainHist), capHist(_capHist), 
  ss(_ss),
  capIndex(0), quietIndex(0)
{
  this->stage = pos.isPseudoLegal(ttMove) ? TT_MOVE : GEN_CAPTURES;

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

    quiets[i].score =
      mainHist[pos.sideToMove][move_from_to(move)]
      + (ss - 1)->contHistory()[chIndex]
      + (ss - 2)->contHistory()[chIndex]
      + (ss - 4)->contHistory()[chIndex];

    i++;
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
    Piece moved   = pos.board[move_from(move)];
    Piece captured = pos.board[move_to(move)];

    if (mt == MT_PROMOTION)
      moveScore += promotionScores[promo_type(move)];
    else {
      if (!pos.see_ge(move, Score(-50)))
        moveScore -= 500000;
      moveScore += PieceValue[mt == MT_EN_PASSANT ? PAWN : captured] * 64;
      moveScore += capHist[pieceTo(pos, move)][captured];
    }

    i++;
  }
}

Move MovePicker::nextMove(bool skipQuiets, MpStage* outStage) {
  select:
  switch (stage)
  {
  case TT_MOVE:
  {
    ++stage;
    *outStage = TT_MOVE;
    return ttMove;
  }
  case GEN_CAPTURES: 
  {
    getStageMoves(pos, false, &captures);
    scoreCaptures();
    ++stage;
    [[fallthrough]];
  }
  case GOOD_CAPTURES:
  {
    if (capIndex < captures.size()) {
      int moveI = nextMoveIndex(captures, capIndex);
      MoveScored move = captures[moveI];
      if (move.score > 0) { // good capture
        captures[moveI] = captures[capIndex++];
        *outStage = GOOD_CAPTURES;
        return move.move;
      }
    }

    if (isQsearch && !pos.checkers)
      return MOVE_NONE;

    ++stage;
    [[fallthrough]];
  }
  case KILLER:
  {
    if (skipQuiets) {
      stage = BAD_CAPTURES;
      goto select;
    }
    ++stage;
    if (pos.isQuiet(killerMove) && pos.isPseudoLegal(killerMove)) {
      *outStage = KILLER;
      return killerMove;
    }
    [[fallthrough]];
  }
  case COUNTER:
  {
    if (skipQuiets) {
      stage = BAD_CAPTURES;
      goto select;
    }
    ++stage;
    if (pos.isQuiet(counterMove) && pos.isPseudoLegal(counterMove)) {
      *outStage = COUNTER;
      return counterMove;
    }
    [[fallthrough]];
  }
  case GEN_QUIETS: 
  {
    if (skipQuiets) {
      stage = BAD_CAPTURES;
      goto select;
    }

    getStageMoves(pos, true, &quiets);
    scoreQuiets();

    ++stage;
    [[fallthrough]];
  }
  case QUIETS: 
  {
    if (skipQuiets) {
      stage = BAD_CAPTURES;
      goto select;
    }
    if (quietIndex < quiets.size()) {
      int moveI = nextMoveIndex(quiets, quietIndex);
      MoveScored move = quiets[moveI];
      quiets[moveI] = quiets[quietIndex++];
      *outStage = QUIETS;
      return move.move;
    }

    ++stage;
    [[fallthrough]];
  }
  case BAD_CAPTURES:
  {
    // If any captures are left, they are all bad
    if (capIndex < captures.size()) {
      int moveI = nextMoveIndex(captures, capIndex);
      MoveScored move = captures[moveI];
      captures[moveI] = captures[capIndex++];
      *outStage = BAD_CAPTURES;
      return move.move;
    }
  }
  }

  return MOVE_NONE;
}