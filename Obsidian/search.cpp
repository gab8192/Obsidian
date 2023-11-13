#include "search.h"
#include "evaluate.h"
#include "history.h"
#include "movegen.h"
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

using namespace Threads;

namespace Search {

  struct SearchLoopInfo {
    Score score;
    Move bestMove;
    int selDepth;
  };

  struct SearchInfo {
    Score staticEval;
    Move playedMove;

    Move killerMove;

    Move pv[MAX_PLY];
    int pvLength;

    Move excludedMove;

    PieceToHistory* mContHistory;

    PieceToHistory& contHistory() {
      return *mContHistory;
    }
  };

  DEFINE_PARAM(LmrBase, 21, -75, 125);
  DEFINE_PARAM(LmrDiv, 224, 150, 300);

  DEFINE_PARAM(StatBonusQuad, 3, 0, 16);
  DEFINE_PARAM(StatBonusLinear, 93, 16, 256);
  DEFINE_PARAM(StatBonusMax, 1200, 800, 2400);
  DEFINE_PARAM(StatBonusBoostAt, 122, 50, 300);

  DEFINE_PARAM(RazoringDepthMul, 406, 400, 800);

  DEFINE_PARAM(RfpDepthMul, 122, 60, 180);

  DEFINE_PARAM(NmpBase, 4, 3, 5);
  DEFINE_PARAM(NmpDepthDiv, 3, 3, 5);
  DEFINE_PARAM(NmpEvalDiv, 200, 100, 400);
  DEFINE_PARAM(NmpEvalDivMin, 3, 2, 6);

  DEFINE_PARAM(LmpBase, 7, 3, 9);
  DEFINE_PARAM(LmpQuad, 1, 1, 3);

  DEFINE_PARAM(PvsSeeMargin, -123, -300, -90);

  DEFINE_PARAM(FutilityBase, 174, 80, 300);
  DEFINE_PARAM(FutilityDepthMul, 118, 80, 300);

  DEFINE_PARAM(LmrHistoryDiv, 7399, 4000, 12000);

  DEFINE_PARAM(AspWindowStartDepth, 5, 4, 8);
  DEFINE_PARAM(AspWindowStartDelta, 10, 10, 20);
  DEFINE_PARAM(AspFailHighReductionMax, 11, 6, 11);
  
  Color rootColor;

  Move lastBestMove;
  clock_t lastSearchTimeSpan;
  bool printingEnabled = true;

  uint64_t nodesSearched;

  int selDepth;

  int rootDepth;

  int ply = 0;

  Key keyStack[MAX_PLY];

  NNUE::Accumulator accumulatorStack[MAX_PLY];

  MoveList rootMoves;

  int lmrTable[MAX_PLY][MAX_MOVES];

  FromToHistory mainHistory;

  // captureHistory[pieceTo][captured]
  int captureHistory[PIECE_NB * SQUARE_NB][PIECE_TYPE_NB];

  ContinuationHistory contHistory;

  Move counterMoveHistory[PIECE_NB][SQUARE_NB];

  int fromTo(Move m) {
    return getMoveSrc(m) * SQUARE_NB + getMoveDest(m);
  }

  int pieceTo(Position& pos, Move m) {
    return pos.board[getMoveSrc(m)] * SQUARE_NB + getMoveDest(m);
  }

  void clear() {

    TT::clear();
    memset(mainHistory, 0, sizeof(mainHistory));
    memset(captureHistory, 0, sizeof(captureHistory));
    memset(counterMoveHistory, 0, sizeof(counterMoveHistory));
    memset(contHistory, 0, sizeof(contHistory));
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

  // Called one at engine initialization
  void searchInit() {

    initLmrTable();

    clear();
  }

  void pushPosition(Position& pos) {
    keyStack[ply] = pos.key;
    memcpy(&accumulatorStack[ply + 1], &accumulatorStack[ply], sizeof(NNUE::Accumulator));

    ply++;
  }

  void popPosition() {
    ply--;
  }

  template<bool root>
  int64_t perft(Position& position, int depth) {

    MoveList moves;
    getPseudoLegalMoves(position, &moves);

    if (depth == 1) {
      int n = 0;
      for (int i = 0; i < moves.size(); i++) {
        if (!position.isLegal(moves[i]))
          continue;

        n++;
      }
      return n;
    }

    int64_t n = 0;
    for (int i = 0; i < moves.size(); i++) {
      if (!position.isLegal(moves[i]))
        continue;

      Position newPos = position;

      newPos.doMove(moves[i], accumulatorStack[ply]);

      int64_t thisNodes = perft<false>(newPos, depth - 1);
      if constexpr (root)
        cout << UCI::move(moves[i]) << " -> " << thisNodes << endl;

      n += thisNodes;
    }
    return n;
  }

  template int64_t perft<false>(Position&, int);
  template int64_t perft<true>(Position&, int);

  enum NodeType {
    Root, PV, NonPV
  };

  clock_t elapsedTime() {
    return timeMillis() - searchSettings.startTime;
  }

  bool usedMostOfTime() {
    if (searchSettings.movetime) {

      clock_t timeLimit = searchSettings.movetime;

      return elapsedTime() >= (timeLimit - 100);
    }
    else if (searchSettings.hasTimeLimit()) {

      clock_t timeLimit = searchSettings.time[rootColor];

      // never use more than 70~80 % of our time
      double d = 0.7;
      if (searchSettings.inc[rootColor])
        d += 0.1;

      return elapsedTime() >= (d * timeLimit - 10);
    }
    return false;
  }

  void playNullMove(Position& pos, SearchInfo* ss) {
    nodesSearched++;

    // Check time
    if ((nodesSearched % 32768) == 0)
      if (usedMostOfTime())
        searchState = STOP_PENDING;

    ss->mContHistory = &contHistory[0];
    ss->playedMove = MOVE_NONE;

    pushPosition(pos);
    pos.doNullMove();
  }

  void playMove(Position& pos, Move move, SearchInfo* ss) {
    nodesSearched++;

    // Check time
    if ((nodesSearched % 32768) == 0)
      if (usedMostOfTime())
        searchState = STOP_PENDING;

    ss->mContHistory = &contHistory[pieceTo(pos, move)];
    ss->playedMove = move;

    pushPosition(pos);
    pos.doMove(move, accumulatorStack[ply]);
  }

  void cancelMove() {
    popPosition();
  }

  int stat_bonus(int d) {
    return std::min(StatBonusQuad * d * d + StatBonusLinear * d, (int)StatBonusMax);
  }

  //        TT move:  MAX
  // Good promotion:  400K
  //   Good capture:  300K
  //        Killers:  200K
  //   Counter-move:  100K
  // Dumb promotion: -100K
  //    Bad capture: -200K

  constexpr int mvv_lva(int captured, int attacker) {
    return PieceValue[captured] * 64 - PieceValue[attacker];
  }

  constexpr int promotionScores[] = {
    0, 0, 400000, -100001, -100000, 410000
  };

  int getHistoryScore(Position& pos, Move move, SearchInfo* ss) {
    int moveScore = mainHistory[pos.sideToMove][fromTo(move)];

    if ((ss - 1)->playedMove)
      moveScore += (ss - 1)->contHistory()[pieceTo(pos, move)];
    if ((ss - 2)->playedMove)
      moveScore += (ss - 2)->contHistory()[pieceTo(pos, move)];

    return moveScore;
  }

  void updateHistories(Position& pos, int depth, Move bestMove, Score bestScore,
                       Score beta, Move* quietMoves, int quietCount, SearchInfo* ss) {

    int bonus = (bestScore > beta + StatBonusBoostAt) ? stat_bonus(depth + 1) : stat_bonus(depth);

    /*
    * Butterfly history
    */
    addToHistory(mainHistory[pos.sideToMove][fromTo(bestMove)], bonus);

    /*
    * Continuation history
    */
    if ((ss - 1)->playedMove)
      addToHistory((ss - 1)->contHistory()[pieceTo(pos, bestMove)], bonus);
    if ((ss - 2)->playedMove)
      addToHistory((ss - 2)->contHistory()[pieceTo(pos, bestMove)], bonus);

    /*
    * Decrease score of other quiet moves
    */
    for (int i = 0; i < quietCount; i++) {
      Move otherMove = quietMoves[i];
      if (otherMove == bestMove)
        continue;

      if ((ss - 1)->playedMove)
        addToHistory((ss - 1)->contHistory()[pieceTo(pos, otherMove)], -bonus);
      if ((ss - 2)->playedMove)
        addToHistory((ss - 2)->contHistory()[pieceTo(pos, otherMove)], -bonus);

      addToHistory(mainHistory[pos.sideToMove][fromTo(otherMove)], -bonus);
    }

    /*
    * Counter move history
    */
    if ((ss - 1)->playedMove) {
      Square prevSq = getMoveDest((ss - 1)->playedMove);
      counterMoveHistory[pos.board[prevSq]][prevSq] = bestMove;
    }

    /*
    * Killer move
    */
    ss->killerMove = bestMove;
  }

  void scoreMoves(Position& pos, MoveList& moves, Move ttMove, SearchInfo* ss) {
    Move killer = ss->killerMove;

    Move counterMove = MOVE_NONE;

    Move prevMove = (ss-1)->playedMove;
    if (prevMove) {
      Square prevSq = getMoveDest(prevMove);
      counterMove = counterMoveHistory[pos.board[prevSq]][prevSq];
    }

    for (int i = 0; i < moves.size(); i++) {
      int& moveScore = moves.scores[i];

      // initial score
      moveScore = 0;

      Move move = moves[i];

      MoveType mt = getMoveType(move);

      Piece moved = pos.board[getMoveSrc(move)];
      Piece captured = pos.board[getMoveDest(move)];

      if (move == ttMove)
        moveScore = INT_MAX;
      else if (mt == MT_PROMOTION)
        moveScore = promotionScores[getPromoType(move)] + PieceValue[captured];
      else if (mt == MT_EN_PASSANT)
        moveScore = 300000 + mvv_lva(PAWN, PAWN);
      else if (captured) {
        moveScore = pos.see_ge(move, Score(-50)) ? 300000 : -200000;
        moveScore += PieceValue[captured] * 64;
        moveScore += captureHistory[pieceTo(pos, move)][ptypeOf(captured)];
      }
      else if (move == killer)
        moveScore = 200000;
      else if (move == counterMove)
        moveScore = 100000;
      else
        moveScore = getHistoryScore(pos, move, ss);
    }
  }

  Move peekBestMove(MoveList& moveList) {
    int bestMoveI = 0;

    int bestMoveScore = moveList.scores[bestMoveI];

    int size = moveList.size();
    for (int i = 0 + 1; i < size; i++) {
      int thisScore = moveList.scores[i];
      if (thisScore > bestMoveScore) {
        bestMoveScore = thisScore;
        bestMoveI = i;
      }
    }

    return moveList[bestMoveI];
  }

  Move nextBestMove(MoveList& moveList, int scannedMoves, int* moveScore) {
    int bestMoveI = scannedMoves;

    int bestMoveScore = moveList.scores[bestMoveI];

    int size = moveList.size();
    for (int i = scannedMoves + 1; i < size; i++) {
      int thisScore = moveList.scores[i];
      if (thisScore > bestMoveScore) {
        bestMoveScore = thisScore;
        bestMoveI = i;
      }
    }

    (*moveScore) = bestMoveScore;

    Move result = moveList[bestMoveI];
    moveList.moves[bestMoveI] = moveList.moves[scannedMoves];
    moveList.scores[bestMoveI] = moveList.scores[scannedMoves];
    return result;
  }

  TT::Flag flagForTT(bool failsHigh) {
    return failsHigh ? TT::FLAG_LOWER : TT::FLAG_UPPER;
  }

  // Should not be called from Root node
  bool is2FoldRepetition(Position& pos) {

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

  Score makeDrawScore() {
    return Score(int(nodesSearched & 2) - 1);
  }

  template<NodeType nodeType>
  Score qsearch(Position& position, Score alpha, Score beta, SearchInfo* ss) {
    constexpr bool PvNode = nodeType != NonPV;

    const Color us = position.sideToMove, them = ~us;

    // detect draw
    if (position.halfMoveClock >= 100)
      return makeDrawScore();

    // Probe TT
    bool ttHit;
    TT::Entry* ttEntry = TT::probe(position.key, ttHit);
    TT::Flag ttFlag = ttHit ? ttEntry->getFlag() : TT::NO_FLAG;
    Score ttScore = ttHit ? ttEntry->getScore() : SCORE_NONE;
    Move ttMove = ttHit ? ttEntry->getMove() : MOVE_NONE;

    // In non PV nodes, if tt bound allows it, return ttScore
    if (!PvNode) {
      if (ttFlag & flagForTT(ttScore >= beta))
        return ttScore;
    }

    Move bestMove = MOVE_NONE;
    Score bestScore;

    // Do the static evaluation

    if (position.checkers) {
      // When in check avoid evaluating
      bestScore = -SCORE_INFINITE;
      ss->staticEval = SCORE_NONE;
    }
    else {

      if (ttHit)
        bestScore = ss->staticEval = ttEntry->getStaticEval();
      else
        bestScore = ss->staticEval = Eval::evaluate(position, accumulatorStack[ply]);

      // When tt bound allows it, use ttScore as a better standing pat
      if (ttFlag & flagForTT(ttScore > bestScore)) {
        bestScore = ttScore;
      }

      if (bestScore >= beta)
        return bestScore;
      if (bestScore > alpha)
        alpha = bestScore;
    }

    // Generate moves and score them

    bool generateAllMoves = position.checkers;
    MoveList moves;
    if (generateAllMoves)
      getPseudoLegalMoves(position, &moves);
    else
      getAggressiveMoves(position, &moves);

    scoreMoves(position, moves, ttMove, ss);

    bool foundLegalMoves = false;

    // Visit moves

    for (int i = 0; i < moves.size(); i++) {
      int moveScore;
      Move move = nextBestMove(moves, i, &moveScore);

      if (!position.isLegal(move))
        continue;

      foundLegalMoves = true;

      // If we are not in check, prevent qsearch from visiting bad captures and under-promotions
      if (bestScore > TB_LOSS_IN_MAX_PLY) {
        if (!generateAllMoves) {
          if (moveScore < -50000)
            break;
        }
      }

      Position newPos = position;

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

    if (position.checkers && !foundLegalMoves)
      return Score(ply - CHECKMATE);

    ttEntry->store(position.key,
      bestScore >= beta ? TT::FLAG_LOWER : TT::FLAG_UPPER,
      0, bestMove, bestScore, ss->staticEval, false);

    return bestScore;
  }

  void updatePV(SearchInfo* ss, Move move) {
    // set the move in the pv
    ss->pv[ply] = move;

    // copy all the moves that follow, from the child pv
    for (int i = ply + 1; i < (ss + 1)->pvLength; i++) {
      ss->pv[i] = (ss + 1)->pv[i];
    }

    ss->pvLength = (ss + 1)->pvLength;
  }

  template<NodeType nodeType>
  Score negaMax(Position& position, Score alpha, Score beta, int depth, bool cutNode, SearchInfo* ss) {
    constexpr bool PvNode = nodeType != NonPV;
    constexpr bool rootNode = nodeType == Root;

    if (searchState == STOP_PENDING)
      return makeDrawScore();

    if (PvNode) {
      // init node
      ss->pvLength = ply;

      if (ply > selDepth)
        selDepth = ply;
    }

    if (!rootNode) {
      // detect draw
      if (is2FoldRepetition(position) || position.halfMoveClock >= 100)
        return makeDrawScore();

      // mate distance pruning
      alpha = std::max(alpha, Score(ply - CHECKMATE));
      beta = std::min(beta, CHECKMATE - ply - 1);
      if (alpha >= beta)
        return alpha;
    }

    // If we are in check, increment the depth and avoid entering a qsearch
    if (position.checkers && !rootNode)
      depth = std::max(1, depth + 1);

    // Enter qsearch when depth is 0
    if (depth <= 0)
      return qsearch<PvNode ? PV : NonPV>(position, alpha, beta, ss);

    (ss + 1)->killerMove = MOVE_NONE;

    Move excludedMove = ss->excludedMove;

    // Probe TT
    bool ttHit;
    TT::Entry* ttEntry = TT::probe(position.key, ttHit);
    TT::Flag ttFlag = ttHit ? ttEntry->getFlag() : TT::NO_FLAG;
    Score ttScore = ttHit ? ttEntry->getScore() : SCORE_NONE;
    Move ttMove = ttHit ? ttEntry->getMove() : MOVE_NONE;

    // Make sure there is a ttMove in rootNode
    if (rootNode && !ttMove)
      ttMove = peekBestMove(rootMoves);

    bool ttMoveNoisy = ttMove && !position.isQuiet(ttMove);

    Score eval;
    Move bestMove = MOVE_NONE;
    Score bestScore = -SCORE_INFINITE;

    // In non PV nodes, if tt depth and bound allow it, return ttScore
    if (!PvNode
      && !excludedMove
      && ttEntry->getDepth() >= depth) 
    {
      if (ttFlag & flagForTT(ttScore >= beta))
        return ttScore;
    }

    bool improving = false;

    // Do the static evaluation

    if (position.checkers) {
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
        ss->staticEval = eval = ttEntry->getStaticEval();
      else
        ss->staticEval = eval = Eval::evaluate(position, accumulatorStack[ply]);

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
    if (eval < alpha - RazoringDepthMul * depth) {
      Score score = qsearch<NonPV>(position, alpha-1, alpha, ss);
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
      && position.hasNonPawns(position.sideToMove)
      && beta > TB_LOSS_IN_MAX_PLY) {

      int R = std::min((eval - beta) / NmpEvalDiv, (int)NmpEvalDivMin) + depth / NmpDepthDiv + NmpBase;

      Position newPos = position;
      playNullMove(newPos, ss);
      Score score = -negaMax<NonPV>(newPos, -beta, -beta + 1, depth - R, !cutNode, ss + 1);
      cancelMove();

      if (score >= beta && abs(score) < TB_WIN_IN_MAX_PLY)
        return score;
    }

    // IIR. Decrement the depth if we expect this search to have bad move ordering
    if ((PvNode || cutNode) && depth >= 4 && !ttMove)
      depth --;

  moves_loop:

    // Generate moves and score them

    const bool wasInCheck = position.checkers;

    MoveList moves;
    if (rootNode) {
      moves = rootMoves;

      for (int i = 0; i < rootMoves.size(); i++)
        rootMoves.scores[i] = -SCORE_INFINITE;
    }
    else {
      getPseudoLegalMoves(position, &moves);
      scoreMoves(position, moves, ttMove, ss);
    }

    bool foundLegalMove = false;

    int playedMoves = 0;

    Move quietMoves[64];
    int quietCount = 0;
    Move captures[64];
    int captureCount = 0;

    bool skipQuiets = false;
    
    // Visit moves

    for (int i = 0; i < moves.size(); i++) {
      int moveScore;
      Move move = nextBestMove(moves, i, &moveScore);

      if (move == excludedMove)
        continue;

      if (!position.isLegal(move))
        continue;

      bool isQuiet = position.isQuiet(move);

      if (isQuiet) {
        if (quietCount < 64)
          quietMoves[quietCount++] = move;

        if (skipQuiets)
          continue;
      }
      else {
        if (captureCount < 64)
          captures[captureCount++] = move;
      }

      foundLegalMove = true;

      if (!rootNode
        && position.hasNonPawns(position.sideToMove)
        && bestScore > TB_LOSS_IN_MAX_PLY)
      {
        // Late move pruning. At low depths, only visit a few quiet moves
        if (quietCount > (LmpQuad * depth * depth + LmpBase) / (2 - improving))
          skipQuiets = true;

        // If this is a capture, do SEE (Static Exchange Evalution) pruning
        if (position.board[getMoveDest(move)]) {
          if (!position.see_ge(move, Score(PvsSeeMargin * depth)))
            continue;
        }

        if (isQuiet) {
          // Futility pruning (~8 Elo). If our evaluation is far below alpha,
          // only visit the first quiet move
          if (depth <= 8 && !wasInCheck && eval + FutilityBase + FutilityDepthMul * depth <= alpha)
            skipQuiets = true;
        }
      }

      int extension = 0;
      
      // Singular extension
      if ( !rootNode
        && ply < 2 * rootDepth
        && depth >= 6
        && !excludedMove
        && move == ttMove
        && abs(ttScore) < TB_WIN_IN_MAX_PLY
        && ttFlag & TT::FLAG_LOWER
        && ttEntry->getDepth() >= depth - 3) 
      {
        Score singularBeta = ttScore - depth;
        
        ss->excludedMove = move;
        Score seScore = negaMax<NonPV>(position, singularBeta - 1, singularBeta, (depth - 1) / 2, cutNode, ss);
        ss->excludedMove = MOVE_NONE;
        
        if (seScore < singularBeta)
          extension = 1;
        else if (singularBeta >= beta) // Multicut
          return singularBeta;
        else if (ttScore >= beta) // Negative extension (~18 Elo)
          extension = -1 + PvNode;
      }

      Position newPos = position;

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

          // Reduce or extend depending on history of this quiet move (~12 Elo)
          if (moveScore > -50000 && moveScore < 50000)
            R -= std::clamp(moveScore / LmrHistoryDiv, -2, 2);

          // Do less reduction for good quiet moves (~4 Elo)
          if (moveScore == 200000 || moveScore == 100000)
            R--;
        }
        else {
          R = 0;

          if (moveScore < 0)
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

      if (rootNode)
        rootMoves.scores[rootMoves.indexOf(move)] = score;

      if (score > bestScore) {
        bestScore = score;

        if (bestScore > alpha) {
          bestMove = move;

          if (PvNode)
            updatePV(ss, bestMove);

          // Always true in NonPV nodes
          if (bestScore >= beta)
            break;

          alpha = bestScore;
        }
      }
    }

    if (!foundLegalMove) {
      if (excludedMove) 
        return alpha;

      return position.checkers ? Score(ply - CHECKMATE) : DRAW;
    }

    // Update histories
    if (bestScore >= beta)
    {
      if (position.isQuiet(bestMove)) 
      {
        updateHistories(position, depth, bestMove, bestScore, beta, quietMoves, quietCount, ss);
      }
      else if (position.board[getMoveDest(bestMove)]) {
        int bonus = stat_bonus(depth);

        {
          Piece captured = position.board[getMoveDest(bestMove)];
          addToHistory(captureHistory[pieceTo(position, bestMove)][ptypeOf(captured)], bonus);
        }

        for (int i = 0; i < captureCount; i++) {
          Move otherMove = captures[i];
          if (otherMove == bestMove)
            continue;

          Piece captured = position.board[getMoveDest(otherMove)];
          addToHistory(captureHistory[pieceTo(position, otherMove)][ptypeOf(captured)], -bonus);
        }
      }
    }

    // Store to TT
    if (!excludedMove) {
      TT::Flag flag;
      if (bestScore >= beta)
        flag = TT::FLAG_LOWER;
      else
        flag = (PvNode && bestMove) ? TT::FLAG_EXACT : TT::FLAG_UPPER;

      ttEntry->store(position.key, flag, depth, bestMove, bestScore, ss->staticEval, PvNode);
    }

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

  constexpr int SsOffset = 4;

  SearchInfo searchStack[MAX_PLY + SsOffset];

  void startSearch() {
    
    Position rootPos = searchSettings.position;
    rootPos.updateAccumulator(accumulatorStack[0]);

    Move bestMove;

    clock_t optimumTime;

    if (searchSettings.hasTimeLimit())
      optimumTime = TimeMan::calcOptimumTime(searchSettings, rootPos.sideToMove);

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
    }

    SearchInfo* ss = &searchStack[SsOffset];

    if (searchSettings.depth == 0)
      searchSettings.depth = MAX_PLY;

    // Setup root moves
    rootMoves = MoveList();
    {
      MoveList pseudoRootMoves;
      getPseudoLegalMoves(rootPos, &pseudoRootMoves);

      for (int i = 0; i < pseudoRootMoves.size(); i++) {
        Move move = pseudoRootMoves[i];
        if (rootPos.isLegal(move))
          rootMoves.add(move);
      }
    }
    // When we have only 1 legal move, play it instantly
    if (rootMoves.size() == 1) {
      bestMove = rootMoves[0];
      goto bestMoveDecided;
    }
    scoreMoves(rootPos, rootMoves, MOVE_NONE, ss);

    for (rootDepth = 1; rootDepth <= searchSettings.depth; rootDepth++) {

      if (searchSettings.nodes && nodesSearched >= searchSettings.nodes)
        break;

      selDepth = 0;

      Score score;
      if (rootDepth >= AspWindowStartDepth) {
        int windowSize = AspWindowStartDelta;
        Score alpha = iterDeepening[rootDepth - 1].score - windowSize;
        Score beta = iterDeepening[rootDepth - 1].score + windowSize;

        int failedHighCnt = 0;
        while (true) {

          int adjustedDepth = std::max(1, rootDepth - failedHighCnt);

          score = negaMax<Root>(rootPos, alpha, beta, adjustedDepth, false, ss);

          if (Threads::searchState == STOP_PENDING)
            goto bestMoveDecided;

          if (searchSettings.nodes && nodesSearched >= searchSettings.nodes)
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
        score = negaMax<Root>(rootPos, -SCORE_INFINITE, SCORE_INFINITE, rootDepth, false, ss);
      }

      // It's super important to not update the best move if the search was abruptly stopped
      if (Threads::searchState == STOP_PENDING)
        goto bestMoveDecided;

      iterDeepening[rootDepth].selDepth = selDepth;
      iterDeepening[rootDepth].score = score;
      iterDeepening[rootDepth].bestMove = bestMove = ss->pv[0];

      clock_t elapsed = elapsedTime();

      if (printingEnabled) {
        ostringstream infoStr;
        infoStr
          << "info"
          << " depth " << rootDepth
          << " seldepth " << selDepth
          << " score " << UCI::score(score)
          << " nodes " << nodesSearched
          << " nps " << (nodesSearched * 1000ULL) / std::max(int(elapsed), 1)
          << " time " << elapsed
          << " pv " << getPvString(ss);
        cout << infoStr.str() << endl;
      }

      if (bestMove == iterDeepening[rootDepth - 1].bestMove)
        searchStability = std::min(searchStability + 1, 8);
      else
        searchStability = 0;

      // Stop searching if we can deliver a forced checkmate.
      // No need to stop if we are getting checkmated, instead keep searching,
      // because we may have overlooked a way out of checkmate due to pruning
      if (score >= CHECKMATE_IN_MAX_PLY)
        goto bestMoveDecided;

      // When playing in movetime mode, stop if we've used 75% time of movetime,
      // because we will probably not hit the next depth in time
      if (searchSettings.movetime)
        if (elapsedTime() >= (searchSettings.movetime * 3) / 4)
          goto bestMoveDecided;

      if (searchSettings.hasTimeLimit() && rootDepth >= 4) {

        // If the position is a dead draw, stop searching
        if (rootDepth >= 40 && abs(score) < 5) {
          goto bestMoveDecided;
        }

        if (usedMostOfTime())
          goto bestMoveDecided;

        double optScale = 1.1 - 0.05 * searchStability;

        if (elapsed > optScale * optimumTime)
          goto bestMoveDecided;
      }
    }

  bestMoveDecided:

    lastBestMove = bestMove;
    lastSearchTimeSpan = elapsedTime();

    if (printingEnabled)
      std::cout << "bestmove " << UCI::move(bestMove) << endl;

    Threads::searchState = IDLE;
  }

  void* idleLoop(void*) {
    while (true) {

      while (Threads::searchState != RUNNING) {
        sleep(1);
      }

      startSearch();
    }
  }
}