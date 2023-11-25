#include "movepick.h"
#include "uci.h"

MovePicker::MovePicker(
  Position& _pos,
  Move _ttMove, Move _killerMove, Move _counterMove,
  MainHistory* _mainHist, CaptureHistory* _capHist,
  PieceToHistory* _ch1, PieceToHistory* _ch2, PieceToHistory* _ch4) :
  pos(_pos),
  ttMove(_ttMove), killerMove(_killerMove), counterMove(_counterMove),
  mainHist(*_mainHist), capHist(*_capHist), 
  contHist1(*_ch1), contHist2(*_ch2), contHist4(*_ch4),
  killerFound(false), counterFound(false), capIndex(0), quietIndex(0)
{
  this->stage = _ttMove ? TT_MOVE : GEN_CAPTURES;

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
  return pos.board[getMoveSrc(m)] * SQUARE_NB + getMoveDest(m);
}

int fromTo(Move m) {
  return getMoveSrc(m) * SQUARE_NB + getMoveDest(m);
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

    if (move == ttMove) {
      quiets.remove(i);
      continue;
    }
    else if (move == killerMove) {
      quiets.remove(i);
      killerFound = true;
      continue;
    }
    else if (move == counterMove) {
      quiets.remove(i);
      counterFound = true;
      continue;
    }

    int chIndex = pieceTo(pos, move);

    quiets[i].score =
      mainHist[pos.sideToMove][fromTo(move)]
      + contHist1[chIndex]
      + contHist2[chIndex]
      + contHist4[chIndex];

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

    MoveType mt = getMoveType(move);

    Piece moved = pos.board[getMoveSrc(move)];
    PieceType captured = ptypeOf(mt == MT_EN_PASSANT ? W_PAWN : pos.board[getMoveDest(move)]);

    moveScore += PieceValue[captured] * 64;

    if (mt == MT_PROMOTION)
      moveScore += promotionScores[getPromoType(move)];
    else if (mt == MT_EN_PASSANT) {}
    else {
      if (!pos.see_ge(move, Score(-50)))
        moveScore -= 500000;
      moveScore += capHist[pieceTo(pos, move)][captured];
    }

    i++;
  }
}

Move MovePicker::nextMove(MpStage* outStage) {
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

    ++stage;
    [[fallthrough]];
  }
  case GEN_QUIETS: 
  {
    getStageMoves(pos, true, &quiets);

    scoreQuiets();

    ++stage;
    [[fallthrough]];
  }
  case KILLER: 
  {
    ++stage;
    if (killerFound) {
      *outStage = KILLER;
      return killerMove;
    }
    [[fallthrough]];
  }
  case COUNTER: 
  {
    ++stage;
    if (counterFound) {
      *outStage = COUNTER;
      return counterMove;
    }
    [[fallthrough]];
  }
  case QUIETS: 
  {
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