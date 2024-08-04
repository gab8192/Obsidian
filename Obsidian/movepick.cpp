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
    if (_killerMove != _ttMove)
      this->killerMove = _killerMove;

    if (_counterMove != _ttMove && _counterMove != _killerMove)
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
  Threats threats;
  pos.calcThreats(threats);
  
  int i = 0;
  while (i < quiets.size()) {
    Move move = quiets[i].move;

    if (move == ttMove || move == killerMove || move == counterMove) {
      quiets.remove(i);
      continue;
    }

    Square from = move_from(move), to = move_to(move);
    PieceType pt = piece_type(pos.board[from]);
    int chIndex = pieceTo(pos, move);

    int threatScore = 0;

    if (pt == QUEEN) {
      if (threats.byRook & from) threatScore += 32768;
      if (threats.byRook & to) threatScore -= 32768;
    } else if (pt == ROOK) {
      if (threats.byMinor & from) threatScore += 16384;
      if (threats.byMinor & to) threatScore -= 16384;
    } else if (pt == KNIGHT || pt == BISHOP) {
      if (threats.byPawn & from) threatScore += 16384;
      if (threats.byPawn & to) threatScore -= 16384;
    }

    quiets[i++].score =
        threatScore
      + mainHist[pos.sideToMove][move_from_to(move)]
      + (ss - 1)->contHistory[chIndex]
      + (ss - 2)->contHistory[chIndex]
      + (ss - 4)->contHistory[chIndex]/2
      + (ss - 6)->contHistory[chIndex]/2;
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

Move MovePicker::nextMove(bool skipQuiets) {
  select:
  switch (stage)
  {
  case IN_CHECK_PLAY_TT:
  case QS_PLAY_TT:
  case PLAY_TT:
  {
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
    if (capIndex < captures.size())
      return nextMove0(captures, capIndex++).move;

    if (stage == QS_PLAY_CAPTURES && !genQuietChecks)
      return MOVE_NONE;

    ++stage;
    goto select;
  }
  case PLAY_GOOD_CAPTURES:
  {
    while (capIndex < captures.size()) {
      Move_Score move = nextMove0(captures, capIndex++);
      int realMargin = searchType == PVS ? (- move.score / 64) : seeMargin;
      if (pos.seeGe(move.move, realMargin) && !isUnderPromo(move.move)) // good capture
        return move.move;
        
      badCaptures.add(move);
    }

    if (searchType == PROBCUT)
      return MOVE_NONE;

    ++stage;
    goto select;
  }
  case PLAY_KILLER:
  {
    ++stage;
    if (pos.isQuiet(killerMove) && pos.isPseudoLegal(killerMove))
      return killerMove;
    goto select;
  }
  case PLAY_COUNTER:
  {
    ++stage;
    if (pos.isQuiet(counterMove) && pos.isPseudoLegal(counterMove))
      return counterMove;
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
    
    if (quietIndex < quiets.size())
      return nextMove0(quiets, quietIndex++).move;

    if (stage == IN_CHECK_PLAY_QUIETS)
      return MOVE_NONE;

    ++stage;
    goto select;
  }
  case PLAY_BAD_CAPTURES:
  {
    if (badCapIndex < badCaptures.size())
      return badCaptures[badCapIndex++].move;
      
    return MOVE_NONE;
  }
  case QS_GEN_QUIET_CHECKS: 
  {
    getQuietChecks(pos, &quiets);
    ++stage;
    goto select;
  }
  case QS_PLAY_QUIET_CHECKS:
  {
    while (quietIndex < quiets.size()) {
      Move move = quiets[quietIndex++].move;
      if (move != ttMove)
        return move;
    }

    return MOVE_NONE;
  }
  }

  return MOVE_NONE;
}