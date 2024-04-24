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
  if (pos.checkers)
    this->stage = IN_CHECK_PLAY_TT;
  else {
    if (_searchType == QSEARCH)
      this->stage = QS_PLAY_TT;
    else
      this->stage = PLAY_TT;
  }

  if (stage == PLAY_TT) {
    if (_killerMove == _ttMove)
      this->killerMove = MOVE_NONE;
    else
      this->killerMove = _killerMove;

    if (_counterMove == _ttMove || _counterMove == _killerMove)
      this->counterMove = MOVE_NONE;
    else
      this->counterMove = _counterMove;
  }

  if (! pos.isPseudoLegal(ttMove))
    ++(this->stage);
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

    MoveType mt = move_type(move);
    PieceType captured = piece_type(pos.board[move_to(move)]);

    captures[i++].score = 
        PIECE_VALUE[mt == MT_EN_PASSANT ? PAWN : captured] * 32
      + (mt == MT_PROMOTION) * 32768
      + capHist[pieceTo(pos, move)][captured];
  }
}

Move MovePicker::nextMove(bool skipQuiets, Stage* outStage) {
  select:
  switch (stage)
  {
  case IN_CHECK_PLAY_TT:
  case QS_PLAY_TT:
  case PLAY_TT:
  {
    *outStage = stage;
    ++stage;
    return ttMove;
  }
  case IN_CHECK_GEN_CAPTURES:
  case QS_GEN_CAPTURES:
  case GEN_CAPTURES: 
  {
    getStageMoves(pos, ADD_CAPTURES, &captures);
    scoreCaptures();
    ++stage;
    goto select;
  }
  case IN_CHECK_PLAY_CAPTURES:
  case QS_PLAY_CAPTURES:
  {
    if (capIndex < captures.size()) {
      Move_Score move = nextMove0(captures, capIndex++);
      *outStage = stage;
      return move.move;
    }

    if (!pos.checkers)
      return MOVE_NONE;

    ++stage;
    goto select;
  }
  case PLAY_GOOD_CAPTURES:
  {
    nextGoodCap:
    if (capIndex < captures.size()) {
      Move_Score move = nextMove0(captures, capIndex++);
      if (pos.seeGe(move.move, seeMargin) && !isUnderPromo(move.move)) { // good capture
        *outStage = stage;
        return move.move;
      }
      badCaptures.add(move);
      goto nextGoodCap;
    }

    if (searchType == PROBCUT)
      return MOVE_NONE;

    ++stage;
    goto select;
  }
  case PLAY_KILLER:
  {
    ++stage;
    if (pos.isQuiet(killerMove) && pos.isPseudoLegal(killerMove)) {
      *outStage = PLAY_KILLER;
      return killerMove;
    }
    goto select;
  }
  case PLAY_COUNTER:
  {
    ++stage;
    if (pos.isQuiet(counterMove) && pos.isPseudoLegal(counterMove)) {
      *outStage = PLAY_COUNTER;
      return counterMove;
    }
    goto select;
  }
  case IN_CHECK_GEN_QUIETS:
  case GEN_QUIETS: 
  {
    if (skipQuiets) {
      stage = PLAY_BAD_CAPTURES;
      goto select;
    }

    getStageMoves(pos, ADD_QUIETS, &quiets);
    scoreQuiets();

    ++stage;
    goto select;
  }
  case IN_CHECK_PLAY_QUIETS:
  case PLAY_QUIETS: 
  {
    if (skipQuiets) {
      stage = PLAY_BAD_CAPTURES;
      goto select;
    }
    
    if (quietIndex < quiets.size()) {
      Move_Score move = nextMove0(quiets, quietIndex++);
      *outStage = stage;
      return move.move;
    }

    if (stage == IN_CHECK_PLAY_QUIETS)
      return MOVE_NONE;

    ++stage;
    goto select;
  }
  case PLAY_BAD_CAPTURES:
  {
    if (badCapIndex < badCaptures.size()) {
      Move_Score move = nextMove0(badCaptures, badCapIndex++);
      *outStage = stage;
      return move.move;
    }
  }
  }

  return MOVE_NONE;
}