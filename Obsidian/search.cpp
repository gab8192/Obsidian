#include "search.h"
#include "cuckoo.h"
#include "evaluate.h"
#include "movepick.h"
#include "fathom/tbprobe.h"
#include "timeman.h"
#include "threads.h"
#include "tt.h"
#include "tuning.h"
#include "uci.h"

#include <climits>
#include <cmath>
#include <sstream>

namespace Search {

  DEFINE_PARAM_S(MpPvsSeeMargin, -30, 15);
  DEFINE_PARAM_S(MpQsSeeMargin, -25, 15);

  DEFINE_PARAM_S(LmrBase, 39, 10);
  DEFINE_PARAM_S(LmrDiv, 211, 10);

  DEFINE_PARAM_S(StatBonusQuad, 3, 1);
  DEFINE_PARAM_S(StatBonusLinear, 116, 10);
  DEFINE_PARAM_S(StatBonusMax, 1159, 50);
  DEFINE_PARAM_S(StatBonusBoostAt, 120, 10);

  DEFINE_PARAM_S(RazoringDepthMul, 415, 10);

  DEFINE_PARAM_S(RfpMaxDepth, 9, 1);
  DEFINE_PARAM_S(RfpDepthMul, 123, 6);

  DEFINE_PARAM_S(NmpBase, 4, 1);
  DEFINE_PARAM_B(NmpDepthDiv, 4, 1, 21);
  DEFINE_PARAM_S(NmpEvalDiv, 207, 20);
  DEFINE_PARAM_S(NmpEvalDivMin, 4, 1);

  DEFINE_PARAM_S(ProbcutBetaMargin, 229, 10);

  DEFINE_PARAM_S(LmpBase,    3, 1);

  DEFINE_PARAM_S(PvsQuietSeeMargin, -77, 20);
  DEFINE_PARAM_S(PvsCapSeeMargin, -132, 20);

  DEFINE_PARAM_S(EarlyLmrHistoryDiv, 5521, 300);

  DEFINE_PARAM_S(FpBase, 182, 10);
  DEFINE_PARAM_S(FpMaxDepth, 8, 1);
  DEFINE_PARAM_S(FpDepthMul, 111, 6);

  DEFINE_PARAM_S(DoubleExtMargin, 16, 2);
  DEFINE_PARAM_S(DoubleExtMax, 6, 1);

  DEFINE_PARAM_S(LmrQuietHistoryDiv, 10486, 300);
  DEFINE_PARAM_S(LmrCapHistoryDiv, 8003, 300);
  DEFINE_PARAM_S(ZwsDeeperMargin, 79, 5);

  DEFINE_PARAM_B(AspWindowStartDepth, 4, 4, 34);
  DEFINE_PARAM_B(AspWindowStartDelta, 11, 5, 45);
  DEFINE_PARAM_B(AspFailHighReductionMax, 11, 1, 21);
  
  bool doingBench = false;

  int lmrTable[MAX_PLY][MAX_MOVES];

  int pieceTo(Position& pos, Move m) {
    return pos.board[move_from(m)] * SQUARE_NB + move_to(m);
  }

  void initLmrTable() {
    // avoid log(0) because it's negative infinity
    lmrTable[0][0] = 0;

    double dBase = LmrBase / 100.0;
    double dDiv = LmrDiv / 100.0;

    for (int i = 1; i < MAX_PLY; i++) {
      for (int m = 1; m < MAX_MOVES; m++) {
        lmrTable[i][m] = dBase + log(i) * log(m) / dDiv;
      }
    }
  }

  // Called once at engine initialization
  void init() {
    initLmrTable();
  }

  void SearchThread::resetHistories() {
    memset(mainHistory, 0, sizeof(mainHistory));
    memset(captureHistory, 0, sizeof(captureHistory));
    memset(counterMoveHistory, 0, sizeof(counterMoveHistory));
    memset(contHistory, 0, sizeof(contHistory));
  }

  SearchThread::SearchThread() :
    thread(std::thread(&SearchThread::idleLoop, this))
  {
    resetHistories();
  }

  template<bool root>
  int64_t perft(Position& pos, int depth) {

    MoveList moves;
    getPseudoLegalMoves(pos, &moves);

    if (depth <= 1) {
      int n = 0;
      for (int i = 0; i < moves.size(); i++)
        n += pos.isLegal(moves[i].move);
      return n;
    }

    int64_t n = 0;
    for (int i = 0; i < moves.size(); i++) {
      Move move = moves[i].move;

      if (!pos.isLegal(move))
        continue;

      DirtyPieces dirtyPieces;

      Position newPos = pos;
      newPos.doMove(move, dirtyPieces);

      int64_t thisNodes = perft<false>(newPos, depth - 1);
      if constexpr (root)
        std::cout << UCI::moveToString(move) << " -> " << thisNodes << std::endl;

      n += thisNodes;
    }
    return n;
  }

  template int64_t perft<false>(Position&, int);
  template int64_t perft<true>(Position&, int);

  clock_t elapsedTime() {
    return timeMillis() - Threads::searchSettings.startTime;
  }

  int stat_bonus(int d) {
    return std::min(StatBonusQuad * d * d + StatBonusLinear * d, (int)StatBonusMax);
  }

  bool SearchThread::usedMostOfTime() {

    if (Threads::searchSettings.hasTimeLimit())
      return elapsedTime() >= maximumTime;
    
    else if (Threads::searchSettings.movetime) {
      clock_t timeLimit = Threads::searchSettings.movetime;
      return elapsedTime() >= (timeLimit - 50);
    }

    return false;
  }

  void SearchThread::playNullMove(Position& pos, SearchInfo* ss) {
    nodesSearched++;

    ss->mContHistory = &contHistory[false][0];
    ss->playedMove = MOVE_NONE;
    keyStack[keyStackHead++] = pos.key;

    ply++;
    pos.doNullMove();
  }

  void SearchThread::cancelNullMove() {
    ply--;
    keyStackHead--;
  }

  void SearchThread::playMove(Position& pos, Move move, SearchInfo* ss) {
    nodesSearched++;

    bool isCap = pos.board[move_to(move)] != NO_PIECE;
    ss->mContHistory = &contHistory[isCap][pieceTo(pos, move)];
    ss->playedMove = move;
    keyStack[keyStackHead++] = pos.key;

    NNUE::Accumulator& oldAcc = accumStack[accumStackHead];
    NNUE::Accumulator& newAcc = accumStack[++accumStackHead];

    DirtyPieces dirtyPieces;

    ply++;
    pos.doMove(move, dirtyPieces);

    TT::prefetch(pos.key);

    newAcc.doUpdates(dirtyPieces, &oldAcc);
  }

  void SearchThread::cancelMove() {
    ply--;
    keyStackHead--;
    accumStackHead--;
  }

  //        TT move:  MAX
  // Good promotion:  400K
  //   Good capture:  300K
  //        Killers:  200K
  //   Counter-move:  100K
  // Dumb promotion: -100K
  //    Bad capture: -200K

  constexpr int promotionScores[] = {
    0, 0, 400000, -100001, -100000, 410000
  };

  int SearchThread::getCapHistory(Position& pos, Move move) {
    PieceType captured = ptypeOf(pos.board[move_to(move)]);
    return captureHistory[pieceTo(pos, move)][captured];
  }

  int SearchThread::getQuietHistory(Position& pos, Move move, SearchInfo* ss) {
    int chIndex = pieceTo(pos, move);
    return    mainHistory[pos.sideToMove][move_from_to(move)]
            + (ss - 1)->contHistory()[chIndex]
            + (ss - 2)->contHistory()[chIndex]
            + (ss - 4)->contHistory()[chIndex];
  }

  void addToContHistory(Position& pos, int bonus, Move move, SearchInfo* ss) {
    int chIndex = pieceTo(pos, move);
    if ((ss - 1)->playedMove)
      addToHistory((ss - 1)->contHistory()[chIndex], bonus);
    if ((ss - 2)->playedMove)              
      addToHistory((ss - 2)->contHistory()[chIndex], bonus);
    if ((ss - 4)->playedMove)              
      addToHistory((ss - 4)->contHistory()[chIndex], bonus);
  }

  void SearchThread::updateHistories(Position& pos, int bonus, Move bestMove, Score bestScore,
                       Score beta, Move* quiets, int quietCount, SearchInfo* ss) {
    // Butterfly history
    addToHistory(mainHistory[pos.sideToMove][move_from_to(bestMove)], bonus);

    // Continuation history
    addToContHistory(pos, bonus, bestMove, ss);

    // Decrease score of other quiet moves
    for (int i = 0; i < quietCount; i++) {
      Move otherMove = quiets[i];
      if (otherMove == bestMove)
        continue;

      addToContHistory(pos, -bonus, otherMove, ss);

      addToHistory(mainHistory[pos.sideToMove][move_from_to(otherMove)], -bonus);
    }

    // Counter move
    if ((ss - 1)->playedMove) {
      Square prevSq = move_to((ss - 1)->playedMove);
      counterMoveHistory[pos.board[prevSq] * SQUARE_NB + prevSq] = bestMove;
    }

    // Killer move
    ss->killerMove = bestMove;
  }

  void SearchThread::scoreRootMoves(Position& pos, MoveList& moves, Move ttMove, SearchInfo* ss) {
    for (int i = 0; i < moves.size(); i++) {
      int& moveScore = moves[i].score;

      // initial score
      moveScore = 0;

      Move move = moves[i].move;

      MoveType mt = move_type(move);
      PieceType captured = ptypeOf(pos.board[move_to(move)]);

      if (move == ttMove)
        moveScore = INT_MAX;
      else if (mt == MT_PROMOTION)
        moveScore = promotionScores[promo_type(move)] + PieceValue[captured] * 128;
      else if (captured || mt == MT_EN_PASSANT) {
        moveScore = pos.see_ge(move, MpPvsSeeMargin) ? 500000 : -500000;
        moveScore += PieceValue[mt == MT_EN_PASSANT ? PAWN : captured] * 128;
        moveScore += getCapHistory(pos, move);
      }
      else
        moveScore = mainHistory[pos.sideToMove][move_from_to(move)];
    }
  }

  Move nextBestMove(MoveList& moveList, int scannedMoves, int* moveScore) {
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

    (*moveScore) = bestMoveScore;

    Move result = moveList[bestMoveI].move;
    moveList[bestMoveI] = moveList[scannedMoves];
    return result;
  }

  TT::Flag boundForTT(bool failsHigh) {
    return failsHigh ? TT::FLAG_LOWER : TT::FLAG_UPPER;
  }

  bool SearchThread::hasUpcomingRepetition(Position& pos, int ply) {

    const Bitboard occ = pos.pieces();
    const int maxDist = std::min(pos.halfMoveClock, keyStackHead);

    for (int i = 3; i <= maxDist; i += 2) {

      Key moveKey = pos.key ^ keyStack[keyStackHead - i];

      int hash = Cuckoo::h1(moveKey);

      // try the other slot
      if (Cuckoo::keys[hash] != moveKey)
        hash = Cuckoo::h2(moveKey);

      if (Cuckoo::keys[hash] != moveKey)
        continue; // neither slot matches

      Move   move = Cuckoo::moves[hash];
      Square from = move_from(move);
      Square to = move_to(move);

      // Check if the move is obstructed
      if ((BetweenBB[from][to] ^ to) & occ)
        continue;

      // Repetition after root
      if (ply > i)
        return true;
      
      Piece pc = pos.board[ pos.board[from] ? from : to ];

      if (colorOf(pc) != pos.sideToMove)
        continue;

      // We want one more repetition before root
      for (int j = i+4; j <= maxDist; j += 2) {
        if (keyStack[keyStackHead - j] == keyStack[keyStackHead - i])
          return true;
      }
    }

    return false;
  }

  bool SearchThread::isRepetition(Position& pos, int ply) {

    const int maxDist = std::min(pos.halfMoveClock, keyStackHead);

    bool hitBeforeRoot = false;

    for (int i = 4; i <= maxDist; i += 2) {
      if (pos.key == keyStack[keyStackHead - i]) {
        if (ply >= i)
          return true;
        if (hitBeforeRoot)
          return true;
        hitBeforeRoot = true;
      }
    }

    return false;
  }

  Score SearchThread::makeDrawScore() {
    return int(nodesSearched & 2) - 1;
  }

  template<bool IsPV>
  Score SearchThread::qsearch(Position& pos, Score alpha, Score beta, SearchInfo* ss) {
    
    // Quit if we are close to reaching max ply
    if (ply >= MAX_PLY-4)
      return pos.checkers ? SCORE_DRAW : Eval::evaluate(pos, accumStack[accumStackHead]);

    // Detect draw
    if (isRepetition(pos, ply) || pos.halfMoveClock >= 100)
      return SCORE_DRAW;

    // Probe TT
    bool ttHit;
    TT::Entry* ttEntry = TT::probe(pos.key, ttHit);
    TT::Flag ttBound = TT::NO_FLAG;
    Score ttScore = SCORE_NONE;
    Move ttMove = MOVE_NONE;
    Score ttStaticEval = SCORE_NONE;

    if (ttHit) {
      ttBound = ttEntry->getBound();
      ttScore = ttEntry->getScore(ply);
      ttMove = ttEntry->getMove();
      ttStaticEval = ttEntry->getStaticEval();
    }

    // In non PV nodes, if tt bound allows it, return ttScore
    if (!IsPV) {
      if (ttBound & boundForTT(ttScore >= beta))
        return ttScore;
    }

    Move bestMove = MOVE_NONE;
    Score bestScore;

    // Do the static evaluation

    if (pos.checkers) {
      // When in check avoid evaluating
      bestScore = -SCORE_INFINITE;
      ss->staticEval = SCORE_NONE;
    }
    else {
      if (ttStaticEval != SCORE_NONE)
        bestScore = ss->staticEval = ttStaticEval;
      else
        bestScore = ss->staticEval = Eval::evaluate(pos, accumStack[accumStackHead]);

      // When tt bound allows it, use ttScore as a better standing pat
      if (ttBound & boundForTT(ttScore > bestScore))
        bestScore = ttScore;

      if (bestScore >= beta)
        return bestScore;
      if (bestScore > alpha)
        alpha = bestScore;
    }

    // Visiting the tt move when it is quiet, and stm is not check, loses ~300 Elo

    bool visitTTMove = (pos.checkers || !pos.isQuiet(ttMove));

    MovePicker movePicker(
      QSEARCH, pos,
      visitTTMove ? ttMove : MOVE_NONE,
      MOVE_NONE, MOVE_NONE,
      mainHistory, captureHistory,
      MpQsSeeMargin,
      ss);

    bool foundLegalMoves = false;

    // Visit moves

    MpStage moveStage;
    Move move;

    while (move = movePicker.nextMove(&moveStage)) {

      // Prevent qsearch from visiting bad captures and under-promotions
      if (bestScore > SCORE_TB_LOSS_IN_MAX_PLY) {
        if (moveStage > QUIETS)
          break;
      }

      if (!pos.isLegal(move))
        continue;

      foundLegalMoves = true;

      Position newPos = pos;
      playMove(newPos, move, ss);

      Score score = -qsearch<IsPV>(newPos, -beta, -alpha, ss + 1);

      cancelMove();

      if (score > bestScore) {
        bestScore = score;

        if (bestScore > alpha) {
          bestMove = move;

          // Always true in NonPV nodes
          if (bestScore >= beta)
            break;

          alpha = bestScore;
        }
      }
    }

    if (pos.checkers && !foundLegalMoves)
      return ply - SCORE_MATE;

    ttEntry->store(pos.key,
      bestScore >= beta ? TT::FLAG_LOWER : TT::FLAG_UPPER,
      0, bestMove, bestScore, ss->staticEval, ttHit && ttEntry->wasPV(), ply);

    return bestScore;
  }

  void updatePV(SearchInfo* ss, int ply, Move move) {
    // set the move in the pv
    ss->pv[ply] = move;

    // copy all the moves that follow, from the child pv
    for (int i = ply + 1; i < (ss + 1)->pvLength; i++) {
      ss->pv[i] = (ss + 1)->pv[i];
    }

    ss->pvLength = (ss + 1)->pvLength;
  }

  TbResult probeTB(Position& pos) {
    if (BitCount(pos.pieces()) > TB_LARGEST)
        return TB_RESULT_FAILED;

    const Square epSquare = pos.epSquare == SQ_NONE ? SQ_A1 : pos.epSquare;

    return tb_probe_wdl(
      pos.pieces(WHITE), pos.pieces(BLACK),
      pos.pieces(KING), pos.pieces(QUEEN), pos.pieces(ROOK),
      pos.pieces(BISHOP), pos.pieces(KNIGHT), pos.pieces(PAWN),
      pos.halfMoveClock, pos.castlingRights, epSquare,
      pos.sideToMove == WHITE);
  }

  template<bool IsPV>
  Score SearchThread::negaMax(Position& pos, Score alpha, Score beta, int depth, bool cutNode, SearchInfo* ss) {

    // Check time
    if ( this == Threads::mainThread() 
      && (nodesSearched & 16383) == 0
      && usedMostOfTime())
        Threads::stopSearch();

    if (Threads::isSearchStopped())
      return SCORE_DRAW;
    
    // Init node
    if (IsPV)
      ss->pvLength = ply;

    // Detect upcoming draw
    if (alpha < SCORE_DRAW && hasUpcomingRepetition(pos, ply)) {
      alpha = makeDrawScore();
      if (alpha >= beta)
        return alpha;
    }

    // Enter qsearch when depth is 0
    if (depth <= 0)
      return qsearch<IsPV>(pos, alpha, beta, ss);

    // Detect draw
    if (isRepetition(pos, ply) || pos.halfMoveClock >= 100)
      return makeDrawScore();

    // Quit if we are close to reaching max ply
    if (ply >= MAX_PLY - 4)
      return pos.checkers ? SCORE_DRAW : Eval::evaluate(pos, accumStack[accumStackHead]);

    // Mate distance pruning
    alpha = std::max(alpha, ply - SCORE_MATE);
    beta = std::min(beta, SCORE_MATE - ply - 1);
    if (alpha >= beta)
      return alpha;

    const Move excludedMove = ss->excludedMove;

    // Probe TT
    bool ttHit;
    TT::Entry* ttEntry = TT::probe(pos.key, ttHit);

    TT::Flag ttBound = TT::NO_FLAG;
    Score ttScore   = SCORE_NONE;
    Move ttMove     = MOVE_NONE;
    int ttDepth     = -1;
    Score ttStaticEval = SCORE_NONE;
    bool ttPV = IsPV;

    if (ttHit) {
      ttBound = ttEntry->getBound();
      ttScore = ttEntry->getScore(ply);
      ttMove = ttEntry->getMove();
      ttDepth = ttEntry->getDepth();
      ttStaticEval = ttEntry->getStaticEval();
      ttPV |= ttEntry->wasPV();
    }

    const bool ttMoveNoisy = ttMove && !pos.isQuiet(ttMove);

    const Score probcutBeta = beta + ProbcutBetaMargin;

    Score eval;
    Move bestMove = MOVE_NONE;
    Score bestScore = -SCORE_INFINITE;
    Score maxScore  =  SCORE_INFINITE; 

    // In non PV nodes, if tt depth and bound allow it, return ttScore
    if ( !IsPV
      && !excludedMove
      && ttDepth >= depth) 
    {
      if (ttBound & boundForTT(ttScore >= beta))
        return ttScore;
    }

    // Probe tablebases
    const TbResult tbResult = excludedMove ? TB_RESULT_FAILED : probeTB(pos);

    if (tbResult != TB_RESULT_FAILED) {

      tbHits++;
      Score tbScore;
      TT::Flag tbBound;

      if (tbResult == TB_LOSS) {
        tbScore = ply - SCORE_TB_WIN;
        tbBound = TT::FLAG_UPPER;
      }
      else if (tbResult == TB_WIN) {
        tbScore = SCORE_TB_WIN - ply;
        tbBound = TT::FLAG_LOWER;
      }
      else {
        tbScore = SCORE_DRAW;
        tbBound = TT::FLAG_EXACT;
      }

      if ((tbBound == TT::FLAG_EXACT) || (tbBound == TT::FLAG_LOWER ? tbScore >= beta : tbScore <= alpha)) {
        ttEntry->store(pos.key, tbBound, depth, MOVE_NONE, tbScore, SCORE_NONE, ttPV, ply);
        return tbScore;
      }

      if (IsPV) {
        if (tbBound == TT::FLAG_LOWER) {
          bestScore = tbScore;
          alpha = std::max(alpha, bestScore);
        } else {
          maxScore = tbScore;
        }
      }
    }

    (ss + 1)->killerMove = MOVE_NONE;
    ss->doubleExt = (ss - 1)->doubleExt;

    bool improving = false;

    // Do the static evaluation

    if (pos.checkers) {
      // When in check avoid evaluating and skip pruning
      ss->staticEval = eval = SCORE_NONE;
      goto moves_loop;
    }
    else if (ss->excludedMove) {
      // We have already evaluated the position in the node which invoked this singular search
      eval = ss->staticEval;
    }
    else {
      if (ttStaticEval != SCORE_NONE)
        ss->staticEval = eval = ttStaticEval;
      else
        ss->staticEval = eval = Eval::evaluate(pos, accumStack[accumStackHead]);

      // When tt bound allows it, use ttScore as a better evaluation
      if (ttBound & boundForTT(ttScore > eval))
        eval = ttScore;
    }

    // Calculate whether the evaluation here is worse or better than it was 2 plies ago
    if ((ss - 2)->staticEval != SCORE_NONE)
      improving = ss->staticEval > (ss - 2)->staticEval;
    else if ((ss - 4)->staticEval != SCORE_NONE)
      improving = ss->staticEval > (ss - 4)->staticEval;

    // Razoring. When evaluation is far below alpha, we could probably only catch up with a capture,
    // thus do a qsearch. If the qsearch still can't hit alpha, cut off
    if ( !IsPV
      && eval < alpha - RazoringDepthMul * depth) {
      Score score = qsearch<IsPV>(pos, alpha, beta, ss);
      if (score <= alpha)
        return score;
    }

    // Reverse futility pruning. When evaluation is far above beta, the opponent is unlikely
    // to catch up, thus cut off
    if ( !IsPV
      && depth <= RfpMaxDepth
      && eval < SCORE_TB_WIN_IN_MAX_PLY
      && eval - RfpDepthMul * (depth - improving) >= beta)
      return eval;

    // Null move pruning. When our evaluation is above beta, we give the opponent
    // a free move, and if he still can't catch up, cut off
    if ( !IsPV
      && !excludedMove
      && (ss - 1)->playedMove != MOVE_NONE
      && eval >= beta
      && pos.hasNonPawns(pos.sideToMove)
      && beta > SCORE_TB_LOSS_IN_MAX_PLY) {

      int R = std::min((eval - beta) / NmpEvalDiv, (int)NmpEvalDivMin) + depth / NmpDepthDiv + NmpBase;

      Position newPos = pos;
      playNullMove(newPos, ss);
      Score score = -negaMax<false>(newPos, -beta, -beta + 1, depth - R, !cutNode, ss + 1);
      cancelNullMove();

      if (score >= beta)
        return score < SCORE_TB_WIN_IN_MAX_PLY ? score : beta;
    }

    // IIR. Decrement the depth if we expect this search to have bad move ordering
    if ((IsPV || cutNode) && depth >= 4 && !ttMove)
      depth --;

    if (   !IsPV
        && !excludedMove
        && depth >= 5
        && std::abs(beta) < SCORE_TB_WIN_IN_MAX_PLY
        && !(ttDepth >= depth - 3 && ttScore < probcutBeta))
    {
      int pcSeeMargin = (probcutBeta - ss->staticEval) * 10 / 16;
      bool visitTTMove = ttMove && !pos.isQuiet(ttMove) && pos.see_ge(ttMove, pcSeeMargin);

      MovePicker pcMovePicker(
        PROBCUT, pos,
        visitTTMove ? ttMove : MOVE_NONE, MOVE_NONE, MOVE_NONE,
        mainHistory, captureHistory,
        pcSeeMargin,
        ss);

      Move move;
      MpStage moveStage;

      while (move = pcMovePicker.nextMove(&moveStage)) {
        if (!pos.isLegal(move))
          continue;

        Position newPos = pos;
        playMove(newPos, move, ss);

        Score score = -qsearch<false>(newPos, -probcutBeta, -probcutBeta + 1, ss + 1);

        // Do a normal search if qsearch was positive
        if (score >= probcutBeta)
          score = -negaMax<false>(newPos, -probcutBeta, -probcutBeta + 1, depth - 4, !cutNode, ss + 1);

        cancelMove();

        if (score >= probcutBeta)
          return score;
      }
    }

  moves_loop:

    // Generate moves and score them

    int seenMoves = 0;
    int playedMoves = 0;

    Move quiets[64];
    int quietCount = 0;
    Move captures[64];
    int captureCount = 0;

    Move counterMove = MOVE_NONE;
    if ((ss - 1)->playedMove) {
      Square prevSq = move_to((ss - 1)->playedMove);
      counterMove = counterMoveHistory[pos.board[prevSq] * SQUARE_NB + prevSq];
    }

    MovePicker movePicker(
      PVS, pos,
      ttMove, ss->killerMove, counterMove,
      mainHistory, captureHistory,
      MpPvsSeeMargin,
      ss);

    // Visit moves

    Move move;
    MpStage moveStage;

    while (move = movePicker.nextMove(& moveStage)) {
      if (move == excludedMove)
        continue;

      if (!pos.isLegal(move))
        continue;
      
      seenMoves++;
      
      bool isQuiet = pos.isQuiet(move);

      int history = isQuiet ? getQuietHistory(pos, move, ss) : getCapHistory(pos, move);

      if ( pos.hasNonPawns(pos.sideToMove)
        && bestScore > SCORE_TB_LOSS_IN_MAX_PLY)
      {
        // SEE (Static Exchange Evalution) pruning
        if (moveStage > GOOD_CAPTURES) {
          int seeMargin = depth * (isQuiet ? PvsQuietSeeMargin : PvsCapSeeMargin);
          if (!pos.see_ge(move, seeMargin))
            continue;
        }

        if (isQuiet) {
          // Late move pruning. At low depths, only visit a few quiet moves
          if (seenMoves >= (depth * depth + LmpBase) / (2 - improving))
            movePicker.stage = BAD_CAPTURES;

          int lmrRed = lmrTable[depth][seenMoves] + !improving - history / EarlyLmrHistoryDiv;
          int lmrDepth = std::max(0, depth - lmrRed);

          // Futility pruning. If our evaluation is far below alpha,
          // only visit a few quiet moves
          if (   lmrDepth <= FpMaxDepth 
              && !pos.checkers 
              && ss->staticEval + FpBase + FpDepthMul * lmrDepth <= alpha)
            movePicker.stage = BAD_CAPTURES;
        }
      }

      int extension = 0;
      
      // Singular extension
      if ( ply < 2 * rootDepth
        && depth >= 6
        && !excludedMove
        && move == ttMove
        && abs(ttScore) < SCORE_TB_WIN_IN_MAX_PLY
        && ttBound & TT::FLAG_LOWER
        && ttDepth >= depth - 3) 
      {
        Score singularBeta = ttScore - depth;
        
        ss->excludedMove = move;
        Score seScore = negaMax<false>(pos, singularBeta - 1, singularBeta, (depth - 1) / 2, cutNode, ss);
        ss->excludedMove = MOVE_NONE;
        
        if (seScore < singularBeta) {
          extension = 1;
          // Extend even more if s. value is smaller than s. beta by some margin
          if (   !IsPV 
              && ss->doubleExt <= DoubleExtMax 
              && seScore < singularBeta - DoubleExtMargin)
          {
            extension = 2;
            ss->doubleExt = (ss - 1)->doubleExt + 1;
          }
        }
        else if (singularBeta >= beta) // Multicut
          return singularBeta;
        else if (ttScore >= beta) // Negative extensions
          extension = -2 + IsPV;
        else if (cutNode)
          extension = -1;
      }

      Position newPos = pos;
      playMove(newPos, move, ss);

      int newDepth = depth + extension - 1;

      Score score;

      // Late move reductions. Search at a reduced depth, moves that are late in the move list

      bool needFullSearch = false;

      if (depth >= 3 && playedMoves >= 1) {
        int R;

        if (isQuiet) {
          R = lmrTable[depth][seenMoves];
          
          // Reduce more if the expected best move is a capture
          R += ttMoveNoisy;

          // Extend killer and counter move
          R -= (moveStage == KILLER || moveStage == COUNTER);

          // Reduce or extend depending on history of this quiet move
          R -= history / LmrQuietHistoryDiv;
        }
        else {
          R = 0;

          R -= history / LmrCapHistoryDiv;
        }

        // Extend moves that give check
        R -= (newPos.checkers != 0ULL);

        // Extend if this position *was* in a PV node. Even further if it *is*
        if (ttPV)
          R -= (1 + IsPV);

        // Reduce if evaluation is trending down
        R += !improving;

        // Reduce if we expect to fail high
        R += 2 * cutNode;

        // Clamp to avoid a qsearch or an extension in the child search
        int reducedDepth = std::clamp(newDepth - R, 1, newDepth + 1);

        score = -negaMax<false>(newPos, -alpha - 1, -alpha, reducedDepth, true, ss + 1);

        if (score > alpha && reducedDepth < newDepth) {
          if (score > bestScore + ZwsDeeperMargin) 
            newDepth++;
          else if (score < bestScore + newDepth)
            newDepth--;
          needFullSearch = reducedDepth < newDepth;
        }
      }
      else
        needFullSearch = !IsPV || playedMoves >= 1;

      if (needFullSearch)
        score = -negaMax<false>(newPos, -alpha - 1, -alpha, newDepth, !cutNode, ss + 1);

      if (IsPV && (playedMoves == 0 || score > alpha))
        score = -negaMax<true>(newPos, -beta, -alpha, newDepth, false, ss + 1);

      cancelMove();

      playedMoves++;
      
      if (isQuiet) {
        if (quietCount < 64)
          quiets[quietCount++] = move;
      }
      else {
        if (captureCount < 64)
          captures[captureCount++] = move;
      }

      if (Threads::isSearchStopped())
        return SCORE_DRAW;

      if (score > bestScore) {
        bestScore = score;

        if (bestScore > alpha) {
          bestMove = move;

          if (IsPV)
            updatePV(ss, ply, bestMove);

          // Always true in NonPV nodes
          if (bestScore >= beta)
            break;

          alpha = bestScore;
        }
      }
    }

    if (!seenMoves) {
      if (excludedMove) 
        return alpha;

      return pos.checkers ? ply - SCORE_MATE : SCORE_DRAW;
    }

    // Update histories
    if (bestScore >= beta)
    {
      int bonus = stat_bonus(depth + (bestScore > beta + StatBonusBoostAt));

      if (pos.isQuiet(bestMove)) 
      {
        updateHistories(pos, bonus, bestMove, bestScore, beta, quiets, quietCount, ss);
      }
      else {
        Piece captured = pos.board[move_to(bestMove)];
        addToHistory(captureHistory[pieceTo(pos, bestMove)][ptypeOf(captured)], bonus);
      }

      for (int i = 0; i < captureCount; i++) {
        Move otherMove = captures[i];
        if (otherMove == bestMove)
          continue;

        Piece captured = pos.board[move_to(otherMove)];
        addToHistory(captureHistory[pieceTo(pos, otherMove)][ptypeOf(captured)], -bonus);
      }
    }

    bestScore = std::min(bestScore, maxScore);

    // Store to TT
    if (!excludedMove) {
      TT::Flag flag;
      if (bestScore >= beta)
        flag = TT::FLAG_LOWER;
      else
        flag = (IsPV && bestMove) ? TT::FLAG_EXACT : TT::FLAG_UPPER;

      ttEntry->store(pos.key, flag, depth, bestMove, bestScore, ss->staticEval, ttPV, ply);
    }

    return bestScore;
  }

  Score SearchThread::rootNegaMax(Position& pos, Score alpha, Score beta, int depth, SearchInfo* ss) {

    // init node
    ss->pvLength = ply;

    // Probe TT
    bool ttHit;
    TT::Entry* ttEntry = TT::probe(pos.key, ttHit);

    TT::Flag ttBound = TT::NO_FLAG;
    Score ttScore = SCORE_NONE;
    Score ttStaticEval = SCORE_NONE;

    if (ttHit) {
      ttBound = ttEntry->getBound();
      ttScore = ttEntry->getScore(ply);
      ttStaticEval = ttEntry->getStaticEval();
    }

    Move ttMove;

    if (depth > 1)
      ttMove = ss->pv[0];
    else
      ttMove = ttHit ? ttEntry->getMove() : MOVE_NONE;

    if (!pos.isPseudoLegal(ttMove))
      ttMove = MOVE_NONE;

    bool ttMoveNoisy = ttMove && !pos.isQuiet(ttMove);

    Score eval;
    Move bestMove = MOVE_NONE;
    Score bestScore = -SCORE_INFINITE;

    (ss + 1)->killerMove = MOVE_NONE;
    ss->doubleExt = 0;

    // Do the static evaluation

    if (pos.checkers) {
      // When in check avoid evaluating and skip pruning
      ss->staticEval = eval = SCORE_NONE;
      goto moves_loop;
    }
    else {
      if (ttStaticEval != SCORE_NONE)
        ss->staticEval = eval = ttStaticEval;
      else
        ss->staticEval = eval = Eval::evaluate(pos, accumStack[accumStackHead]);

      // When tt bound allows it, use ttScore as a better evaluation
      if (ttBound & boundForTT(ttScore > eval))
        eval = ttScore;
    }

  moves_loop:

    // Generate moves and score them

    MoveList moves = rootMoves;

    scoreRootMoves(pos, moves, ttMove, ss);

    bool foundLegalMove = false;

    int playedMoves = 0;

    Move quiets[64];
    int quietCount = 0;
    Move captures[64];
    int captureCount = 0;

    // Visit moves

    for (int i = 0; i < moves.size(); i++) {
      int moveScore;
      Move move = nextBestMove(moves, i, &moveScore);

      if (!pos.isLegal(move))
        continue;

      bool isQuiet = pos.isQuiet(move);

      if (isQuiet) {
        if (quietCount < 64)
          quiets[quietCount++] = move;
      }
      else {
        if (captureCount < 64)
          captures[captureCount++] = move;
      }

      foundLegalMove = true;

      int oldNodesCount = nodesSearched;

      Position newPos = pos;
      playMove(newPos, move, ss);

      int newDepth = depth - 1;

      Score score;

      // Late move reductions. Search at a reduced depth, moves that are late in the move list

      bool needFullSearch;

      if (depth >= 3 && playedMoves > 3) {
        int R;

        if (isQuiet) {
          R = lmrTable[depth][playedMoves + 1];
        }
        else {
          R = 0;

          if (moveScore < 0)
            R++;
        }

        if (newPos.checkers)
          R--;

        // Do the clamp to avoid a qsearch or an extension in the child search
        int reducedDepth = std::clamp(newDepth - R, 1, newDepth + 1);

        score = -negaMax<false>(newPos, -alpha - 1, -alpha, reducedDepth, true, ss + 1);

        needFullSearch = score > alpha && reducedDepth < newDepth;
      }
      else
        needFullSearch = playedMoves >= 1;

      if (needFullSearch)
        score = -negaMax<false>(newPos, -alpha - 1, -alpha, newDepth, true, ss + 1);

      if (playedMoves == 0 || score > alpha)
        score = -negaMax<true>(newPos, -beta, -alpha, newDepth, false, ss + 1);

      cancelMove();

      playedMoves++;
      
      rootMoves[rootMoves.indexOf(move)].nodes += nodesSearched - oldNodesCount;

      if (Threads::isSearchStopped())
        return SCORE_DRAW;

      if (score > bestScore) {
        bestScore = score;

        if (bestScore > alpha) {
          bestMove = move;

          updatePV(ss, ply, bestMove);

          // Always true in NonPV nodes
          if (bestScore >= beta)
            break;

          alpha = bestScore;
        }
      }
    }

    if (!foundLegalMove)
      return pos.checkers ? ply - SCORE_MATE : SCORE_DRAW;

    // Update histories
    if (bestScore >= beta)
    {
      int bonus = stat_bonus(depth + (bestScore > beta + StatBonusBoostAt));

      if (pos.isQuiet(bestMove))
      {
        updateHistories(pos, bonus, bestMove, bestScore, beta, quiets, quietCount, ss);
      }
      else {
        Piece captured = pos.board[move_to(bestMove)];
        addToHistory(captureHistory[pieceTo(pos, bestMove)][ptypeOf(captured)], bonus);
      }

      for (int i = 0; i < captureCount; i++) {
        Move otherMove = captures[i];
        if (otherMove == bestMove)
          continue;

        Piece captured = pos.board[move_to(otherMove)];
        addToHistory(captureHistory[pieceTo(pos, otherMove)][ptypeOf(captured)], -bonus);
      }
    }

    // Store to TT
    TT::Flag flag;
    if (bestScore >= beta)
      flag = TT::FLAG_LOWER;
    else
      flag = bestMove ? TT::FLAG_EXACT : TT::FLAG_UPPER;

    ttEntry->store(pos.key, flag, depth, bestMove, bestScore, ss->staticEval, true, ply);

    return bestScore;
  }

  std::string getPvString(SearchInfo* ss) {

    std::ostringstream output;

    output << UCI::moveToString(ss->pv[0]);

    for (int i = 1; i < ss->pvLength; i++) {
      Move move = ss->pv[i];
      if (!move)
        break;

      output << ' ' << UCI::moveToString(move);
    }

    return output.str();
  }

  DEFINE_PARAM_B(tm0, 177, 50, 200);
  DEFINE_PARAM_B(tm1, 63,  20, 100);

  DEFINE_PARAM_B(tm2, 141, 50, 200);
  DEFINE_PARAM_B(tm3, 4,   0, 30);

  DEFINE_PARAM_B(tm4, 96,   0, 150);
  DEFINE_PARAM_B(tm5, 10,   0, 150);

  DEFINE_PARAM_B(lol0, -12, -150, 0);
  DEFINE_PARAM_B(lol1, 56,   0,  150);

  void SearchThread::startSearch() {

    Position rootPos = Threads::searchSettings.position;

    accumStackHead = 0;
    rootPos.updateAccumulator(accumStack[accumStackHead]);

    keyStackHead = 0;
    for (int i = 0; i < Threads::searchSettings.prevPositions.size(); i++)
      keyStack[keyStackHead++] = Threads::searchSettings.prevPositions[i];

    Move bestMove;

    if (Threads::searchSettings.hasTimeLimit())
      TimeMan::calcOptimumTime(Threads::searchSettings, rootPos.sideToMove,
                              &optimumTime, &maximumTime);

    ply = 0;

    nodesSearched = 0;

    tbHits = 0;

    SearchLoopInfo iterDeepening[MAX_PLY];

    for (int i = 0; i < MAX_PLY + SsOffset; i++) {
      searchStack[i].staticEval = SCORE_NONE;

      searchStack[i].pvLength = 0;

      searchStack[i].killerMove   = MOVE_NONE;
      searchStack[i].excludedMove = MOVE_NONE;
      searchStack[i].playedMove   = MOVE_NONE;

      searchStack[i].mContHistory = &contHistory[false][0];

      searchStack[i].doubleExt = 0;
    }

    int searchStability = 0;

    SearchInfo* ss = &searchStack[SsOffset];

    if (Threads::searchSettings.depth == 0)
      Threads::searchSettings.depth = MAX_PLY-1;

    clock_t startTimeForBench = timeMillis();

    // Setup root moves
    rootMoves = MoveList();
    {
      MoveList pseudoRootMoves;
      getPseudoLegalMoves(rootPos, &pseudoRootMoves);

      for (int i = 0; i < pseudoRootMoves.size(); i++) {
        Move move = pseudoRootMoves[i].move;
        if (rootPos.isLegal(move))
          rootMoves.add(move);
      }
    }
    // When we have only 1 legal move, play it instantly
    if (rootMoves.size() == 1) {
      bestMove = rootMoves[0].move;
      goto bestMoveDecided;
    }

    for (int i = 0; i < rootMoves.size(); i++)
      rootMoves[i].nodes = 0;

    for (rootDepth = 1; rootDepth <= Threads::searchSettings.depth; rootDepth++) {

      if (Threads::searchSettings.nodes && nodesSearched >= Threads::searchSettings.nodes)
        break;

      Score score;
      if (rootDepth >= AspWindowStartDepth) {
        int windowSize = AspWindowStartDelta;
        Score alpha = std::max(-SCORE_INFINITE, iterDeepening[rootDepth - 1].score - windowSize);
        Score beta  = std::min( SCORE_INFINITE, iterDeepening[rootDepth - 1].score + windowSize);

        int failedHighCnt = 0;
        while (true) {

          int adjustedDepth = std::max(1, rootDepth - failedHighCnt);

          score = rootNegaMax(rootPos, alpha, beta, adjustedDepth, ss);

          if (Threads::isSearchStopped())
            goto bestMoveDecided;

          if (Threads::searchSettings.nodes && nodesSearched >= Threads::searchSettings.nodes)
            break; // only break, in order to print info about the partial search we've done

          if (score >= SCORE_MATE_IN_MAX_PLY) {
            beta = SCORE_INFINITE;
            failedHighCnt = 0;
          }

          if (score <= alpha) {
            beta = (alpha + beta) / 2;
            alpha = std::max(-SCORE_INFINITE, alpha - windowSize);

            failedHighCnt = 0;
          }
          else if (score >= beta) {
            beta = std::min(SCORE_INFINITE, beta + windowSize);

            failedHighCnt = std::min((int)AspFailHighReductionMax, failedHighCnt + 1);
          }
          else
            break;

          windowSize += windowSize / 3;
        }
      }
      else {
        score = rootNegaMax(rootPos, -SCORE_INFINITE, SCORE_INFINITE, rootDepth, ss);
      }

      // It's super important to not update the best move if the search was abruptly stopped
      if (Threads::isSearchStopped())
        goto bestMoveDecided;

      iterDeepening[rootDepth].score = score;
      iterDeepening[rootDepth].bestMove = bestMove = ss->pv[0];

      if (this != Threads::mainThread())
        continue;

      clock_t elapsed = elapsedTime();
      clock_t elapsedStrict = timeMillis() - startTimeForBench;

      if (!doingBench) {
        std::ostringstream infoStr;
        infoStr
          << "info"
          << " depth " << rootDepth
          << " score " << UCI::scoreToString(score)
          << " nodes " << Threads::totalNodes()
          << " nps " << (Threads::totalNodes() * 1000ULL) / std::max(elapsedStrict, 1L)
          << " tbhits " << Threads::totalTbHits()
          << " time " << elapsed
          << " pv " << getPvString(ss);
        std::cout << infoStr.str() << std::endl;
      }

      // Stop searching if we can deliver a forced checkmate.
      // No need to stop if we are getting checkmated, instead keep searching,
      // because we may have overlooked a way out of checkmate due to pruning
      if (score >= SCORE_MATE_IN_MAX_PLY)
        goto bestMoveDecided;

      // When playing in movetime mode, stop if we've used 75% time of movetime,
      // because we will probably not hit the next depth in time
      if (Threads::searchSettings.movetime)
        if (elapsedTime() >= (Threads::searchSettings.movetime * 3) / 4)
          goto bestMoveDecided;

      if (bestMove == iterDeepening[rootDepth - 1].bestMove)
        searchStability = std::min(searchStability + 1, 8);
      else
        searchStability = 0;

      if (Threads::searchSettings.hasTimeLimit() && rootDepth >= 4) {

        // If the position is a dead draw, stop searching
        if (rootDepth >= 40 && abs(score) < 5) {
          goto bestMoveDecided;
        }

        if (usedMostOfTime())
          goto bestMoveDecided;

        int bmNodes = rootMoves[rootMoves.indexOf(bestMove)].nodes;
        double notBestNodes = 1.0 - (bmNodes / double(nodesSearched));
        double nodesFactor     = (tm1/100.0) + notBestNodes * (tm0/100.0);

        double stabilityFactor = (tm2/100.0) - searchStability * (tm3/100.0);

        int scoreLoss = std::clamp<int>(iterDeepening[rootDepth - 1].score - score, lol0, lol1);
        double scoreFactor     = (tm4/100.0) + scoreLoss * (tm5/1000.0);

        if (elapsed > stabilityFactor * nodesFactor * scoreFactor * optimumTime)
          goto bestMoveDecided;
      }
    }

  bestMoveDecided:

    if (this == Threads::mainThread() && !doingBench) 
      std::cout << "bestmove " << UCI::moveToString(bestMove) << std::endl;
  }

  void SearchThread::idleLoop() {
    while (true) {
      std::unique_lock lock(mutex);
      cv.wait(lock, [&] { return searching; });

      if (exitThread)
          return;

   // searching = true; (already done by the UCI thread)
      startSearch();
      searching = false;

      cv.notify_all();

      if (this == Threads::mainThread())
        Threads::stopSearch();
    }
  }
}