#include "search.h"
#include "evaluate.h"
#include "movepick.h"
#include "timeman.h"
#include "threads.h"
#include "tt.h"
#include "tuning.h"
#include "uci.h"

#include <climits>
#include <cmath>
#include <sstream>

#ifdef USE_AVX2
#include <immintrin.h>
#endif

namespace Search {

  DEFINE_PARAM(LmrBase, 21, -75, 125);
  DEFINE_PARAM(LmrDiv, 224, 150, 300);

  DEFINE_PARAM(StatBonusQuad, 3, 0, 16);
  DEFINE_PARAM(StatBonusLinear, 112, 16, 256);
  DEFINE_PARAM(StatBonusMax, 1213, 800, 2400);
  DEFINE_PARAM(StatBonusBoostAt, 130, 50, 300);

  DEFINE_PARAM(RazoringDepthMul, 406, 400, 800);

  DEFINE_PARAM(RfpDepthMul, 122, 60, 180);

  DEFINE_PARAM(NmpBase, 4, 3, 5);
  DEFINE_PARAM(NmpDepthDiv, 3, 3, 5);
  DEFINE_PARAM(NmpEvalDiv, 200, 100, 400);
  DEFINE_PARAM(NmpEvalDivMin, 3, 2, 6);

  DEFINE_PARAM(LmpBase, 7, 3, 9);
  DEFINE_PARAM(LmpQuad, 1, 1, 3);

  DEFINE_PARAM(PvsQuietSeeMargin, -87, -300, 0);
  DEFINE_PARAM(PvsCapSeeMargin, -123, -300, 0);

  DEFINE_PARAM(FpBase, 177, 50, 350);
  DEFINE_PARAM(FpMaxDepth, 8, 0, 30);
  DEFINE_PARAM(FpDepthMul, 117, 50, 350);

  DEFINE_PARAM(LmrHistoryDiv, 9828, 4000, 16000);

  DEFINE_PARAM(AspWindowStartDepth, 5, 4, 8);
  DEFINE_PARAM(AspWindowStartDelta, 10, 10, 20);
  DEFINE_PARAM(AspFailHighReductionMax, 11, 6, 11);
  
  Move lastBestMove;
  clock_t lastSearchTimeSpan;
  bool doingBench = false;

  int lmrTable[MAX_PLY][MAX_MOVES];

  int fromTo(Move m) {
    return move_from(m) * SQUARE_NB + move_to(m);
  }

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
  void searchInit() {

    initLmrTable();
  }

  void SearchThread::resetHistories() {
    memset(mainHistory, 0, sizeof(mainHistory));
    memset(captureHistory, 0, sizeof(captureHistory));
    memset(counterMoveHistory, 0, sizeof(counterMoveHistory));
    memset(contHistory, 0, sizeof(contHistory));
  }

  SearchThread::SearchThread() :
    thread(std::thread(&SearchThread::idleLoop, this)),
    running(false), stopThread(false)
  {
    resetHistories();
  }

  template<bool root>
  int64_t perft(Position& pos, int depth) {

    MoveList moves;
    getPseudoLegalMoves(pos, &moves);

    if (depth == 1) {
      int n = 0;
      for (int i = 0; i < moves.size(); i++) {
        if (!pos.isLegal(moves[i].move))
          continue;

        n++;
      }
      return n;
    }

    int64_t n = 0;
    for (int i = 0; i < moves.size(); i++) {
      Move move = moves[i].move;

      if (!pos.isLegal(move))
        continue;

      Position newPos = pos;
      NNUE::Accumulator fakeAcc;
      newPos.doMove(move, fakeAcc);

      int64_t thisNodes = perft<false>(newPos, depth - 1);
      if constexpr (root)
        cout << UCI::move(move) << " -> " << thisNodes << endl;

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
    if (Threads::searchSettings.movetime) {

      clock_t timeLimit = Threads::searchSettings.movetime;

      return elapsedTime() >= (timeLimit - 100);
    }
    else if (Threads::searchSettings.hasTimeLimit()) {

      clock_t timeLimit = Threads::searchSettings.time[rootColor];

      // never use more than ~80 % of our time
      return elapsedTime() >= (0.8 * timeLimit - 50);
    }
    return false;
  }

  void SearchThread::playNullMove(Position& pos, SearchInfo* ss) {
    nodesSearched++;

    ss->mContHistory = &contHistory[false][0];
    ss->playedMove = MOVE_NONE;
    keyStack[ply] = pos.key;

    memcpy(&accumulatorStack[ply + 1], &accumulatorStack[ply], sizeof(NNUE::Accumulator));

    ply++;
    pos.doNullMove();
  }

  void SearchThread::playMove(Position& pos, Move move, SearchInfo* ss) {
    nodesSearched++;

    // Prefetch the TT entry
    if (move_type(move) == MT_NORMAL)
      TT::prefetch(pos.keyAfter(move));

    bool isCap = pos.board[move_to(move)] != NO_PIECE;
    ss->mContHistory = &contHistory[isCap][pieceTo(pos, move)];
    ss->playedMove = move;
    keyStack[ply] = pos.key;

    memcpy(&accumulatorStack[ply + 1], &accumulatorStack[ply], sizeof(NNUE::Accumulator));

    ply++;
    pos.doMove(move, accumulatorStack[ply]);
  }

  void SearchThread::cancelMove() {
    ply--;
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

  int SearchThread::getHistoryScore(Position& pos, Move move, SearchInfo* ss) {

    return    mainHistory[pos.sideToMove][fromTo(move)]
            + (ss - 1)->contHistory()[pieceTo(pos, move)]
            + (ss - 2)->contHistory()[pieceTo(pos, move)]
            + (ss - 4)->contHistory()[pieceTo(pos, move)];
  }

  void addToContHistory(Position& pos, int bonus, Move move, SearchInfo* ss) {
    int moved = pieceTo(pos, move);

    if ((ss - 1)->playedMove)
      addToHistory((ss - 1)->contHistory()[moved], bonus);
    if ((ss - 2)->playedMove)              
      addToHistory((ss - 2)->contHistory()[moved], bonus);
    if ((ss - 4)->playedMove)              
      addToHistory((ss - 4)->contHistory()[moved], bonus);
  }

  void SearchThread::updateHistories(Position& pos, int bonus, Move bestMove, Score bestScore,
                       Score beta, Move* quietMoves, int quietCount, SearchInfo* ss) {

    // Butterfly history
    addToHistory(mainHistory[pos.sideToMove][fromTo(bestMove)], bonus);

    // Continuation history
    addToContHistory(pos, bonus, bestMove, ss);

    // Decrease score of other quiet moves
    for (int i = 0; i < quietCount; i++) {
      Move otherMove = quietMoves[i];
      if (otherMove == bestMove)
        continue;

      addToContHistory(pos, -bonus, otherMove, ss);

      addToHistory(mainHistory[pos.sideToMove][fromTo(otherMove)], -bonus);
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

      Piece moved = pos.board[move_from(move)];
      Piece captured = pos.board[move_to(move)];

      if (move == ttMove)
        moveScore = INT_MAX;
      else if (mt == MT_PROMOTION)
        moveScore = promotionScores[promo_type(move)] + PieceValue[captured];
      else if (mt == MT_EN_PASSANT)
        moveScore = 300000 + PieceValue[PAWN] * 64;
      else if (captured) {
        moveScore = pos.see_ge(move, Score(-10)) ? 300000 : -200000;
        moveScore += PieceValue[captured] * 64;
        moveScore += captureHistory[pieceTo(pos, move)][ptypeOf(captured)];
      }
      else
        moveScore = mainHistory[pos.sideToMove][fromTo(move)];
    }
  }

  Move peekBestMove(MoveList& moveList) {
    int bestMoveI = 0;

    int bestMoveScore = moveList[bestMoveI].score;

    int size = moveList.size();
    for (int i = 0 + 1; i < size; i++) {
      int thisScore = moveList[i].score;
      if (thisScore > bestMoveScore) {
        bestMoveScore = thisScore;
        bestMoveI = i;
      }
    }

    return moveList[bestMoveI].move;
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

  TT::Flag flagForTT(bool failsHigh) {
    return failsHigh ? TT::FLAG_LOWER : TT::FLAG_UPPER;
  }

  // Should not be called from Root node
  bool SearchThread::is2FoldRepetition(Position& pos) {

    if (pos.halfMoveClock < 4)
      return false;

    for (int i = ply - 2; i >= 0; i -= 2) {
      if (pos.key == keyStack[i])
        return true;
    }

    // Start at last-1 because posStack[0] = seenPositions[last]
    for (int i = seenPositions.size() + (ply&1) - 3; i >= 0; i -= 2) {
      if (pos.key == seenPositions[i])
        return true;
    }

    return false;
  }

  Score SearchThread::makeDrawScore() {
    return Score(int(nodesSearched & 2) - 1);
  }

  template<NodeType nodeType>
  Score SearchThread::qsearch(Position& pos, Score alpha, Score beta, SearchInfo* ss) {
    constexpr bool PvNode = nodeType != NonPV;
    
    // Quit if we are close to reaching max ply
    if (ply >= MAX_PLY-4)
      return pos.checkers ? DRAW : Eval::evaluate(pos, accumulatorStack[ply]);

    // Detect draw
    if (pos.halfMoveClock >= 100)
      return makeDrawScore();

    // Probe TT
    bool ttHit;
    TT::Entry* ttEntry = TT::probe(pos.key, ttHit);
    TT::Flag ttFlag = TT::NO_FLAG;
    Score ttScore = SCORE_NONE;
    Move ttMove = MOVE_NONE;
    Score ttStaticEval = SCORE_NONE;
    if (ttHit) {
      ttFlag = ttEntry->getFlag();
      ttScore = ttEntry->getScore(ply);
      ttMove = ttEntry->getMove();
      ttStaticEval = ttEntry->getStaticEval();
    }

    // In non PV nodes, if tt bound allows it, return ttScore
    if (!PvNode) {
      if (ttFlag & flagForTT(ttScore >= beta))
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

      if (ttHit)
        bestScore = ss->staticEval = ttStaticEval;
      else
        bestScore = ss->staticEval = Eval::evaluate(pos, accumulatorStack[ply]);

      // When tt bound allows it, use ttScore as a better standing pat
      if (ttFlag & flagForTT(ttScore > bestScore)) {
        bestScore = ttScore;
      }

      if (bestScore >= beta)
        return bestScore;
      if (bestScore > alpha)
        alpha = bestScore;
    }

    // Visiting the tt move when it is quiet, and stm is not check, loses ~300 Elo

    bool visitTTMove = (pos.checkers || !pos.isQuiet(ttMove));

    MovePicker movePicker(
      true, pos,
      visitTTMove ? ttMove : MOVE_NONE,
      MOVE_NONE, MOVE_NONE,
      mainHistory, captureHistory,
      ss);

    bool foundLegalMoves = false;

    // Visit moves

    MpStage moveStage;
    Move move;

    while (move = movePicker.nextMove(false, &moveStage)) {

      if (!pos.isLegal(move))
        continue;

      foundLegalMoves = true;

      // Prevent qsearch from visiting bad captures and under-promotions
      if (bestScore > TB_LOSS_IN_MAX_PLY) {
        if (moveStage > QUIETS)
          break;
      }

      Position newPos = pos;
      playMove(newPos, move, ss);

      Score score = -qsearch<nodeType>(newPos, -beta, -alpha, ss + 1);

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
      return Score(ply - CHECKMATE);

    ttEntry->store(pos.key,
      bestScore >= beta ? TT::FLAG_LOWER : TT::FLAG_UPPER,
      0, bestMove, bestScore, ss->staticEval, false, ply);

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

  template<NodeType nodeType>
  Score SearchThread::negaMax(Position& pos, Score alpha, Score beta, int depth, bool cutNode, SearchInfo* ss) {
    constexpr bool PvNode = nodeType != NonPV;

    if (Threads::getSearchState() != RUNNING)
      return DRAW;
  
    // Init node
    if (PvNode)
      ss->pvLength = ply;

    // Check time
    if (this == Threads::mainThread() && (nodesSearched % 16384) == 0) {
      if (usedMostOfTime()) {
        Threads::stopSearch(false);
        return DRAW;
      }
    }

    // Detect draw
    if (is2FoldRepetition(pos) || pos.halfMoveClock >= 100)
      return makeDrawScore();

    // Enter qsearch when depth is 0
    if (depth <= 0)
      return qsearch<PvNode ? PV : NonPV>(pos, alpha, beta, ss);

    // Quit if we are close to reaching max ply
    if (ply >= MAX_PLY - 4)
      return pos.checkers ? DRAW : Eval::evaluate(pos, accumulatorStack[ply]);

    // Mate distance pruning
    alpha = std::max(alpha, Score(ply - CHECKMATE));
    beta = std::min(beta, CHECKMATE - ply - 1);
    if (alpha >= beta)
      return alpha;

    Move excludedMove = ss->excludedMove;

    // Probe TT
    bool ttHit;
    TT::Entry* ttEntry = TT::probe(pos.key, ttHit);

    TT::Flag ttFlag = TT::NO_FLAG;
    Score ttScore   = SCORE_NONE;
    Move ttMove     = MOVE_NONE;
    int ttDepth     = -1;
    Score ttStaticEval = SCORE_NONE;

    if (ttHit) {
      ttFlag = ttEntry->getFlag();
      ttScore = ttEntry->getScore(ply);
      ttMove = ttEntry->getMove();
      ttDepth = ttEntry->getDepth();
      ttStaticEval = ttEntry->getStaticEval();
    }

    bool ttMoveNoisy = ttMove && !pos.isQuiet(ttMove);

    Score eval;
    Move bestMove = MOVE_NONE;
    Score bestScore = -SCORE_INFINITE;

    // In non PV nodes, if tt depth and bound allow it, return ttScore
    if (!PvNode
      && !excludedMove
      && ttDepth >= depth) 
    {
      if (ttFlag & flagForTT(ttScore >= beta))
        return ttScore;
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
      if (ttHit)
        ss->staticEval = eval = ttStaticEval;
      else
        ss->staticEval = eval = Eval::evaluate(pos, accumulatorStack[ply]);

      // When tt bound allows it, use ttScore as a better evaluation
      if (ttFlag & flagForTT(ttScore > eval))
        eval = ttScore;
    }

    // Calculate whether the evaluation here is worse or better than it was 2 plies ago
    if ((ss - 2)->staticEval != SCORE_NONE)
      improving = ss->staticEval > (ss - 2)->staticEval;
    else if ((ss - 4)->staticEval != SCORE_NONE)
      improving = ss->staticEval > (ss - 4)->staticEval;

    // Razoring. When evaluation is far below alpha, we could probably only catch up with a capture,
    // thus do a qsearch. If the qsearch still can't hit alpha, cut off
    if (!PvNode
      && eval < alpha - RazoringDepthMul * depth) {
      Score score = qsearch<NonPV>(pos, alpha-1, alpha, ss);
      if (score < alpha)
        return score;
    }

    // Reverse futility pruning. When evaluation is far above beta, the opponent is unlikely
    // to catch up, thus cut off
    if (!PvNode
      && depth < 9
      && abs(eval) < TB_WIN_IN_MAX_PLY
      && eval >= beta
      && eval - RfpDepthMul * (depth - improving) >= beta)
      return eval;

    // Null move pruning. When our evaluation is above beta, we give the opponent
    // a free move, and if he still can't catch up, cut off
    if (!PvNode
      && !excludedMove
      && (ss - 1)->playedMove != MOVE_NONE
      && eval >= beta
      && pos.hasNonPawns(pos.sideToMove)
      && beta > TB_LOSS_IN_MAX_PLY) {

      int R = std::min((eval - beta) / NmpEvalDiv, (int)NmpEvalDivMin) + depth / NmpDepthDiv + NmpBase;

      Position newPos = pos;
      playNullMove(newPos, ss);
      Score score = -negaMax<NonPV>(newPos, -beta, -beta + 1, depth - R, !cutNode, ss + 1);
      cancelMove();

      if (score >= beta)
        return score < TB_WIN_IN_MAX_PLY ? score : beta;
    }

    // IIR. Decrement the depth if we expect this search to have bad move ordering
    if ((PvNode || cutNode) && depth >= 4 && !ttMove)
      depth --;

  moves_loop:

    // Generate moves and score them

    const bool wasInCheck = pos.checkers;

    bool foundLegalMove = false;

    int playedMoves = 0;

    Move quietMoves[64];
    int quietCount = 0;
    Move captures[64];
    int captureCount = 0;

    Move counterMove = MOVE_NONE;
    if ((ss - 1)->playedMove) {
      Square prevSq = move_to((ss - 1)->playedMove);
      counterMove = counterMoveHistory[pos.board[prevSq] * SQUARE_NB + prevSq];
    }

    MovePicker movePicker(
      false, pos,
      ttMove, ss->killerMove, counterMove,
      mainHistory, captureHistory,
      ss);

    bool skipQuiets = false;
    
    // Visit moves

    Move move;
    MpStage moveStage;

    while (move = movePicker.nextMove(skipQuiets, & moveStage)) {
      if (move == excludedMove)
        continue;

      if (!pos.isLegal(move))
        continue;

      bool isQuiet = pos.isQuiet(move);

      if (isQuiet) {
        if (quietCount < 64)
          quietMoves[quietCount++] = move;
      }
      else {
        if (captureCount < 64)
          captures[captureCount++] = move;
      }

      foundLegalMove = true;

      if ( pos.hasNonPawns(pos.sideToMove)
        && bestScore > TB_LOSS_IN_MAX_PLY)
      {
        // SEE (Static Exchange Evalution) pruning
        if (moveStage > GOOD_CAPTURES) {
          int seeMargin = depth * (isQuiet ? PvsQuietSeeMargin : PvsCapSeeMargin);
          if (!pos.see_ge(move, Score(seeMargin)))
            continue;
        }

        if (isQuiet && !skipQuiets) {

          int lmrRed = lmrTable[depth][playedMoves + 1] - PvNode + !improving;
          int lmrDepth = std::max(0, depth - lmrRed);

          // Late move pruning. At low depths, only visit a few quiet moves
          if (quietCount >= (LmpQuad * depth * depth + LmpBase) / (2 - improving))
            skipQuiets = true;

          // Futility pruning (~8 Elo). If our evaluation is far below alpha,
          // only visit the first quiet move
          if (lmrDepth <= FpMaxDepth && !wasInCheck && eval + FpBase + FpDepthMul * lmrDepth <= alpha)
            skipQuiets = true;
        }
      }

      int extension = 0;
      
      // Singular extension
      if ( ply < 2 * rootDepth
        && depth >= 6
        && !excludedMove
        && move == ttMove
        && abs(ttScore) < TB_WIN_IN_MAX_PLY
        && ttFlag & TT::FLAG_LOWER
        && ttDepth >= depth - 3) 
      {
        Score singularBeta = ttScore - depth;
        
        ss->excludedMove = move;
        Score seScore = negaMax<NonPV>(pos, singularBeta - 1, singularBeta, (depth - 1) / 2, cutNode, ss);
        ss->excludedMove = MOVE_NONE;
        
        if (seScore < singularBeta) {
          extension = 1;
          // Extend even more if s. value is smaller than s. beta by some margin
          if (!PvNode && ss->doubleExt <= 5 && seScore < singularBeta - 17) {
            extension = 2;
            ss->doubleExt = (ss - 1)->doubleExt + 1;
          }
        }
        else if (singularBeta >= beta) // Multicut
          return singularBeta;
        else if (ttScore >= beta) // Negative extension (~18 Elo)
          extension = -1 + PvNode;
      }

      int oldNodesCount = nodesSearched;

      Position newPos = pos;
      playMove(newPos, move, ss);

      int newDepth = depth + extension - 1;

      Score score;

      // Late move reductions. Search at a reduced depth, moves that are late in the move list

      bool needFullSearch;

      if (depth >= 3 && playedMoves > (1 + 2 * PvNode)) {
        int R;

        if (isQuiet) {
          R = lmrTable[depth][playedMoves + 1];

          R += !improving;

          // Reduce more if ttmove was noisy (~6 Elo)
          R += ttMoveNoisy;

          // Do less reduction for killer and counter move (~4 Elo)
          if (moveStage == KILLER || moveStage == COUNTER)
            R--;
          // Reduce or extend depending on history of this quiet move (~12 Elo)
          else 
            R -= std::clamp(getHistoryScore(pos, move, ss) / LmrHistoryDiv, -2, 2);
        }
        else {
          R = 0;

          if (moveStage > QUIETS)
            R++;
        }

        if (newPos.checkers)
          R --;

        R -= PvNode;

        R += cutNode;

        // Do the clamp to avoid a qsearch or an extension in the child search
        int reducedDepth = std::clamp(newDepth - R, 1, newDepth + 1);

        score = -negaMax<NonPV>(newPos, -alpha - 1, -alpha, reducedDepth, true, ss + 1);

        needFullSearch = score > alpha && reducedDepth < newDepth;
      }
      else
        needFullSearch = !PvNode || playedMoves >= 1;


      if (needFullSearch)
        score = -negaMax<NonPV>(newPos, -alpha - 1, -alpha, newDepth, !cutNode, ss + 1);

      if (PvNode && (playedMoves == 0 || score > alpha))
        score = -negaMax<PV>(newPos, -beta, -alpha, newDepth, false, ss + 1);

      cancelMove();

      playedMoves++;

      if (score > bestScore) {
        bestScore = score;

        if (bestScore > alpha) {
          bestMove = move;

          if (PvNode)
            updatePV(ss, ply, bestMove);

          // Always true in NonPV nodes
          if (bestScore >= beta)
            break;

          alpha = bestScore;
        }
      }
    }
    
    if (Threads::getSearchState() != RUNNING)
      return DRAW;

    if (!foundLegalMove) {
      if (excludedMove) 
        return alpha;

      return pos.checkers ? Score(ply - CHECKMATE) : DRAW;
    }

    // Update histories
    if (bestScore >= beta)
    {
      int bonus = (bestScore > beta + StatBonusBoostAt) ? stat_bonus(depth + 1) : stat_bonus(depth);

      if (pos.isQuiet(bestMove)) 
      {
        updateHistories(pos, bonus, bestMove, bestScore, beta, quietMoves, quietCount, ss);
      }
      else if (pos.board[move_to(bestMove)]) {
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
    if (!excludedMove) {
      TT::Flag flag;
      if (bestScore >= beta)
        flag = TT::FLAG_LOWER;
      else
        flag = (PvNode && bestMove) ? TT::FLAG_EXACT : TT::FLAG_UPPER;

      ttEntry->store(pos.key, flag, depth, bestMove, bestScore, ss->staticEval, PvNode, ply);
    }

    return bestScore;
  }

  Score SearchThread::rootNegaMax(Position& pos, Score alpha, Score beta, int depth, SearchInfo* ss) {

    // init node
    ss->pvLength = ply;

    // Probe TT
    bool ttHit;
    TT::Entry* ttEntry = TT::probe(pos.key, ttHit);

    TT::Flag ttFlag = TT::NO_FLAG;
    Score ttScore = SCORE_NONE;
    Score ttStaticEval = SCORE_NONE;

    if (ttHit) {
      ttFlag = ttEntry->getFlag();
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

    scoreRootMoves(pos, rootMoves, ttMove, ss);

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
      if (ttHit)
        ss->staticEval = eval = ttStaticEval;
      else
        ss->staticEval = eval = Eval::evaluate(pos, accumulatorStack[ply]);

      // When tt bound allows it, use ttScore as a better evaluation
      if (ttFlag & flagForTT(ttScore > eval))
        eval = ttScore;
    }

  moves_loop:

    // Generate moves and score them

    const bool wasInCheck = pos.checkers;

    MoveList moves = rootMoves;

    bool foundLegalMove = false;

    int playedMoves = 0;

    Move quietMoves[64];
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
          quietMoves[quietCount++] = move;
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

        score = -negaMax<NonPV>(newPos, -alpha - 1, -alpha, reducedDepth, true, ss + 1);

        needFullSearch = score > alpha && reducedDepth < newDepth;
      }
      else
        needFullSearch = playedMoves >= 1;


      if (needFullSearch)
        score = -negaMax<NonPV>(newPos, -alpha - 1, -alpha, newDepth, true, ss + 1);

      if (playedMoves == 0 || score > alpha)
        score = -negaMax<PV>(newPos, -beta, -alpha, newDepth, false, ss + 1);

      cancelMove();

      playedMoves++;
      
      rootMoves[rootMoves.indexOf(move)].nodes += nodesSearched - oldNodesCount;

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

    if (Threads::getSearchState() != RUNNING)
      return DRAW;

    if (!foundLegalMove)
      return pos.checkers ? Score(ply - CHECKMATE) : DRAW;

    // Update histories
    if (bestScore >= beta)
    {
      int bonus = (bestScore > beta + StatBonusBoostAt) ? stat_bonus(depth + 1) : stat_bonus(depth);

      if (pos.isQuiet(bestMove))
      {
        updateHistories(pos, bonus, bestMove, bestScore, beta, quietMoves, quietCount, ss);
      }
      else if (pos.board[move_to(bestMove)]) {
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

    ostringstream output;

    output << UCI::move(ss->pv[0]);

    for (int i = 1; i < ss->pvLength; i++) {
      Move move = ss->pv[i];
      if (!move)
        break;

      output << ' ' << UCI::move(move);
    }

    return output.str();
  }

  DEFINE_PARAM(tm0, 168, 0, 400);
  DEFINE_PARAM(tm1, 56, 0, 200);
  DEFINE_PARAM(tm2, 127, 0, 300);
  DEFINE_PARAM(tm3, 3, 0, 40);

  void SearchThread::startSearch() {

    Position rootPos = Threads::searchSettings.position;
    rootPos.updateAccumulator(accumulatorStack[0]);

    Move bestMove;

    clock_t optimumTime;

    if (Threads::searchSettings.hasTimeLimit())
      optimumTime = TimeMan::calcOptimumTime(Threads::searchSettings, rootPos.sideToMove);

    int searchStability = 0;

    ply = 0;

    nodesSearched = 0;

    rootColor = rootPos.sideToMove;

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

    for (rootDepth = 1; rootDepth <= Threads::searchSettings.depth; rootDepth++) {

      if (Threads::searchSettings.nodes && nodesSearched >= Threads::searchSettings.nodes)
        break;

      for (int i = 0; i < rootMoves.size(); i++)
        rootMoves[i].nodes = 0;

      Score score;
      if (rootDepth >= AspWindowStartDepth) {
        int windowSize = AspWindowStartDelta;
        Score alpha = iterDeepening[rootDepth - 1].score - windowSize;
        Score beta = iterDeepening[rootDepth - 1].score + windowSize;

        int failedHighCnt = 0;
        while (true) {

          int adjustedDepth = std::max(1, rootDepth - failedHighCnt);

          score = rootNegaMax(rootPos, alpha, beta, adjustedDepth, ss);

          if (Threads::getSearchState() != RUNNING)
            goto bestMoveDecided;

          if (Threads::searchSettings.nodes && nodesSearched >= Threads::searchSettings.nodes)
            break; // only break, in order to print info about the partial search we've done

          if (score >= CHECKMATE_IN_MAX_PLY) {
            beta = SCORE_INFINITE;
            failedHighCnt = 0;
          }

          if (score <= alpha) {
            beta = Score((alpha + beta) / 2);
            alpha = (Score)std::max(-SCORE_INFINITE, alpha - windowSize);

            failedHighCnt = 0;
          }
          else if (score >= beta) {
            beta = (Score)std::min(SCORE_INFINITE, beta + windowSize);

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
      if (Threads::getSearchState() != RUNNING)
        goto bestMoveDecided;

      iterDeepening[rootDepth].score = score;
      iterDeepening[rootDepth].bestMove = bestMove = ss->pv[0];

      if (this != Threads::mainThread())
        continue;

      clock_t elapsed = elapsedTime();
      clock_t elapsedStrict = timeMillis() - startTimeForBench;

      if (!doingBench) {
        ostringstream infoStr;
        infoStr
          << "info"
          << " depth " << rootDepth
          << " score " << UCI::score(score)
          << " nodes " << Threads::totalNodes()
          << " nps " << (Threads::totalNodes() * 1000ULL) / std::max(elapsedStrict, 1L)
          << " time " << elapsed
          << " pv " << getPvString(ss);
        cout << infoStr.str() << endl;
      }

      // Stop searching if we can deliver a forced checkmate.
      // No need to stop if we are getting checkmated, instead keep searching,
      // because we may have overlooked a way out of checkmate due to pruning
      if (score >= CHECKMATE_IN_MAX_PLY)
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

        int idNodes = 0;
        for (int i = 0; i < rootMoves.size(); i++)
          idNodes += rootMoves[i].nodes;

        int bmNodes = rootMoves[rootMoves.indexOf(bestMove)].nodes;
        double notBestNodes = 1.0 - (bmNodes / double(idNodes));
        double nodesFactor = notBestNodes * (tm0/100.0) + (tm1/100.0);

        double stabilityFactor = (tm2/100.0) - (tm3/100.0) * searchStability;

        if (elapsed > stabilityFactor * nodesFactor * optimumTime)
          goto bestMoveDecided;
      }
    }

  bestMoveDecided:

    if (this == Threads::mainThread()) {
      lastBestMove = bestMove;
      lastSearchTimeSpan = timeMillis() - startTimeForBench;

      if (!doingBench)
        std::cout << "bestmove " << UCI::move(bestMove) << endl;
    }
  }

  void SearchThread::idleLoop() {
    while (true) {

      while (Threads::getSearchState() != RUNNING) {

        if (stopThread)
          return;

        sleep(1);
      }

      this->running = true;
      startSearch();
      if (this == Threads::mainThread())
        Threads::onSearchComplete();
      this->running = false;
    }
  }
}