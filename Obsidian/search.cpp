#include "search.h"
#include "evaluate.h"
#include "history.h"
#include "movegen.h"
#include "timeman.h"
#include "threads.h"
#include "tt.h"
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
    Value score;
    Move bestMove;
    int selDepth;
  };

  struct SearchInfo {
    Value staticEval;
    Move playedMove;

    Move killers[2];

    Move pv[MAX_PLY];
    int pvLength;

    Move excludedMove;

    PieceToHistory* mContHistory;

    PieceToHistory& contHistory() {
      return *mContHistory;
    }
  };

  Color rootColor;

  Move lastBestMove;
  clock_t lastSearchTimeSpan;
  bool printingEnabled = true;

  uint64_t nodesSearched;

  int selDepth;

  int rootDepth;

  int ply = 0;

  NNUE::Accumulator accumulatorStack[MAX_PLY];

  Position posStack[MAX_PLY];

  Position position;
  MoveList rootMoves;

  int lmrTable[MAX_PLY][MAX_MOVES];

  FromToHistory mainHistory;

  ContinuationHistory contHistory;

  Move counterMoveHistory[PIECE_NB][SQUARE_NB];

  Piece pieceOn(Square sq) {
    return position.board[sq];
  }

  int fromTo(Move m) {
    return getMoveSrc(m) * SQUARE_NB + getMoveDest(m);
  }

  int pieceTo(Move m) {
    return pieceOn(getMoveSrc(m)) * SQUARE_NB + getMoveDest(m);
  }

  void clear() {

    TT::clear();
    memset(mainHistory, 0, sizeof(mainHistory));
    memset(counterMoveHistory, 0, sizeof(counterMoveHistory));
    memset(contHistory, 0, sizeof(contHistory));
  }

  // Called one at engine initialization
  void searchInit() {

    // avoid log(0) because it's negative infinity
    lmrTable[0][0] = 0;

    for (int i = 1; i < MAX_PLY; i++) {
      for (int m = 1; m < MAX_MOVES; m++) {
        lmrTable[i][m] = 0.25 + log(i) * log(m) / 2.25;
      }
    }

    clear();
  }

  NNUE::Accumulator* currentAccumulator() {
    return &accumulatorStack[ply];
  }

  inline void pushPosition() {
    memcpy(&posStack[ply], &position, sizeof(Position));

    memcpy(&accumulatorStack[ply + 1], &accumulatorStack[ply], sizeof(NNUE::Accumulator));

    ply++;
  }

  inline void popPosition() {
    ply--;

    memcpy(&position, &posStack[ply], sizeof(Position));
  }

  template<bool root>
  int perft(int depth) {
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

    int n = 0;
    for (int i = 0; i < moves.size(); i++) {
      if (!position.isLegal(moves[i]))
        continue;

      pushPosition();
      position.doMove(moves[i], &accumulatorStack[0]);

      int thisNodes = perft<false>(depth - 1);
      if constexpr (root)
        cout << UCI::move(moves[i]) << " -> " << thisNodes << endl;

      popPosition();

      n += thisNodes;
    }
    return n;
  }

  template int perft<false>(int);
  template int perft<true>(int);

  enum NodeType {
    Root, PV, NonPV
  };

  inline clock_t elapsedTime() {
    return timeMillis() - searchLimits.startTime;
  }

  bool usedMostOfTime() {
    // never use more than 70~80 % of our time
    double d = 0.7;
    if (searchLimits.inc[rootColor])
      d += 0.1;

    return elapsedTime() >= (d * searchLimits.time[rootColor] - 10);
  }

  void checkTime() {
    if (!searchLimits.hasTimeLimit())
      return;

    if (usedMostOfTime())
      searchState = STOP_PENDING;
  }

  inline void playNullMove(SearchInfo* ss) {
    nodesSearched++;
    if ((nodesSearched % 32768) == 0)
      checkTime();

    ss->mContHistory = &contHistory[0];
    ss->playedMove = MOVE_NONE;

    pushPosition();
    position.doNullMove();
  }

  inline void playMove(Move move, SearchInfo* ss) {
    nodesSearched++;
    if ((nodesSearched % 32768) == 0)
      checkTime();

    ss->mContHistory = &contHistory[pieceTo(move)];
    ss->playedMove = move;

    pushPosition();
    position.doMove(move, &accumulatorStack[ply]);
  }

  inline void cancelMove() {
    popPosition();
  }

  int stat_bonus(int d) {
    return std::min(2 * d * d + 16 * d, 1000);
  }

  //        TT move:  MAX
  // Good promotion:  400K
  //   Good capture:  300K
  //        Killers:  200K
  //   Counter-move:  100K
  // Dumb promotion: -100K
  //    Bad capture: -200K

  constexpr int mvv_lva(int captured, int attacker) {
    return PieceValue[captured] * 100 - PieceValue[attacker];
  }

  constexpr int promotionScores[] = {
    0, 0, 400000, -100001, -100000, 410000
  };

  void scoreMoves(MoveList& moves, Move ttMove, SearchInfo* ss) {
    Move killer0 = ss->killers[0],
      killer1 = ss->killers[1];

    Move counterMove = MOVE_NONE;

    Move prevMove = (ss-1)->playedMove;
    if (prevMove) {
      Square prevSq = getMoveDest(prevMove);
      counterMove = counterMoveHistory[pieceOn(prevSq)][prevSq];
    }

    for (int i = 0; i < moves.size(); i++) {
      int& moveScore = moves.scores[i];

      // initial value
      moveScore = 0;

      Move move = moves[i];

      MoveType mt = getMoveType(move);

      Piece moved = pieceOn(getMoveSrc(move));
      Piece captured = pieceOn(getMoveDest(move));

      if (move == ttMove)
        moveScore = INT_MAX;
      else if (mt == MT_PROMOTION)
        moveScore = promotionScores[getPromoType(move)] + PieceValue[captured];
      else if (mt == MT_EN_PASSANT)
        moveScore = 300000 + mvv_lva(PAWN, PAWN);
      else if (captured) {
        if (position.see_ge(move, VALUE_DRAW))
          moveScore = 300000 + mvv_lva(captured, moved);
        else
          moveScore = -200000 + mvv_lva(captured, moved);
      }
      else if (move == killer0)
        moveScore = 200001;
      else if (move == killer1)
        moveScore = 200000;
      else if (move == counterMove)
        moveScore = 100000;
      else {
        moveScore = mainHistory[position.sideToMove][fromTo(move)];

        if ((ss - 1)->playedMove)
          moveScore += (ss - 1)->contHistory()[pieceTo(move)];
        if ((ss - 2)->playedMove)
          moveScore += (ss - 2)->contHistory()[pieceTo(move)];
      }
    }
  }

  Move nextBestMove(MoveList& moveList, int scannedMoves) {
    int bestMoveI = scannedMoves;

    int bestMoveValue = moveList.scores[bestMoveI];

    int size = moveList.size();
    for (int i = scannedMoves + 1; i < size; i++) {
      int thisValue = moveList.scores[i];
      if (thisValue > bestMoveValue) {
        bestMoveValue = thisValue;
        bestMoveI = i;
      }
    }

    Move result = moveList[bestMoveI];
    moveList.moves[bestMoveI] = moveList.moves[scannedMoves];
    moveList.scores[bestMoveI] = moveList.scores[scannedMoves];
    return result;
  }

  inline TT::Flag flagForTT(bool failsHigh) {
    return failsHigh ? TT::FLAG_LOWER : TT::FLAG_UPPER;
  }

  // Should not be called from Root node
  bool is2FoldRepetition() {

    if (position.halfMoveClock < 4)
      return false;

    for (int i = ply - 2; i >= 0; i -= 2) {
      if (position.key == posStack[i].key)
        return true;
    }

    // Start at last-1 because posStack[0] = seenPositions[last]
    for (int i = seenPositions.size() + (ply&1) - 3; i >= 0; i -= 2) {
      if (position.key == seenPositions[i])
        return true;
    }

    return false;
  }

  inline Value makeDrawValue() {
    return Value(int(nodesSearched % 3ULL) - 1);
  }

  template<NodeType nodeType>
  Value qsearch(Value alpha, Value beta, SearchInfo* ss) {
    constexpr bool PvNode = nodeType != NonPV;

    const Color us = position.sideToMove, them = ~us;

    if (position.halfMoveClock >= 100)
      return makeDrawValue();

    bool ttHit;
    TT::Entry* ttEntry = TT::probe(position.key, ttHit);
    TT::Flag ttFlag = ttHit ? ttEntry->getFlag() : TT::NO_FLAG;
    Value ttValue = ttHit ? ttEntry->getValue() : VALUE_NONE;
    Move ttMove = ttHit ? ttEntry->getMove() : MOVE_NONE;

    if (!PvNode) {
      if (ttFlag & flagForTT(ttValue >= beta))
        return ttValue;
    }

    Move bestMove = MOVE_NONE;
    Value bestValue;
    const Value oldAlpha = alpha;

    if (position.checkers) {
      bestValue = -VALUE_INFINITE;
      ss->staticEval = VALUE_NONE;
    }
    else {

      if (ttHit)
        bestValue = ss->staticEval = ttEntry->getStaticEval();
      else
        bestValue = ss->staticEval = Eval::evaluate();

      if (ttFlag & flagForTT(ttValue > bestValue)) {
        bestValue = ttValue;
      }

      if (bestValue >= beta)
        return bestValue;
      if (bestValue > alpha)
        alpha = bestValue;
    }
    bool generateAllMoves = position.checkers;
    MoveList moves;
    if (generateAllMoves)
      getPseudoLegalMoves(position, &moves);
    else
      getAggressiveMoves(position, &moves);

    scoreMoves(moves, ttMove, ss);

    bool foundLegalMoves = false;

    for (int i = 0; i < moves.size(); i++) {
      Move move = nextBestMove(moves, i);
      if (!position.isLegal(move))
        continue;

      foundLegalMoves = true;

      if (bestValue > VALUE_TB_LOSS_IN_MAX_PLY) {
        if (!generateAllMoves) {
          if (!position.see_ge(move, Value(-50)))
            continue;
        }
      }

      playMove(move, ss);

      Value value = -qsearch<nodeType>(-beta, -alpha, ss + 1);

      cancelMove();

      if (value > bestValue) {
        bestValue = value;

        if (bestValue > alpha) {
          bestMove = move;

          // Always true in NonPV nodes
          if (bestValue >= beta)
            break;

          // This is never reached on a NonPV node
          alpha = bestValue;
        }
      }
    }

    if (position.checkers && !foundLegalMoves)
      return Value(ply - VALUE_MATE);

    TT::Flag flag;
    if (bestValue >= beta)
      flag = TT::FLAG_LOWER;
    else
      flag = (alpha > oldAlpha ? TT::FLAG_EXACT : TT::FLAG_UPPER);

    ttEntry->store(position.key, flag, 0, bestMove, bestValue, ss->staticEval);

    return bestValue;
  }

  inline void updatePV(SearchInfo* ss, Move move) {
    // set the move in the pv
    ss->pv[ply] = move;

    // copy all the moves that follow, from the child pv
    for (int i = ply + 1; i < (ss + 1)->pvLength; i++) {
      ss->pv[i] = (ss + 1)->pv[i];
    }

    ss->pvLength = (ss + 1)->pvLength;
  }

  template<NodeType nodeType>
  Value negaMax(Value alpha, Value beta, int depth, bool cutNode, SearchInfo* ss) {
    constexpr bool PvNode = nodeType != NonPV;
    constexpr bool rootNode = nodeType == Root;

    const Color us = position.sideToMove, them = ~us;

    if (PvNode) {
      // init node
      ss->pvLength = ply;

      if (ply > selDepth)
        selDepth = ply;
    }

    if (searchState == STOP_PENDING)
      return makeDrawValue();

    (ss + 1)->killers[0] = MOVE_NONE;
    (ss + 1)->killers[1] = MOVE_NONE;

    if (!rootNode) {
      if (is2FoldRepetition() || position.halfMoveClock >= 100)
        return makeDrawValue();

      // mate distance pruning
      alpha = std::max(alpha, Value(ply - VALUE_MATE));
      beta = std::min(beta, VALUE_MATE - ply - 1);
      if (alpha >= beta)
        return alpha;
    }

    Move excludedMove = ss->excludedMove;

    bool ttHit;
    TT::Entry* ttEntry = TT::probe(position.key, ttHit);
    TT::Flag ttFlag = ttHit ? ttEntry->getFlag() : TT::NO_FLAG;
    Value ttValue = ttHit ? ttEntry->getValue() : VALUE_NONE;
    Move ttMove = ttHit ? ttEntry->getMove() : MOVE_NONE;

    if (rootNode) {
      if (!ttMove)
        ttMove = rootMoves[0];
    }

    Value eval;
    Move bestMove = MOVE_NONE;
    Value bestValue = -VALUE_INFINITE;

    if (position.checkers && !rootNode)
      depth = std::max(1, depth + 1);

    if (!PvNode
      && !excludedMove
      && ttEntry->getDepth() >= depth) {

      if (ttFlag & flagForTT(ttValue >= beta))
        return ttValue;
    }

    if (depth <= 0)
      return qsearch<PvNode ? PV : NonPV>(alpha, beta, ss);

    bool improving = false;

    // Static evaluation of the position
    if (position.checkers) {
      ss->staticEval = eval = VALUE_NONE;

      // skip pruning when in check
      goto moves_loop;
    }
    else if (ss->excludedMove) {
      eval = ss->staticEval;
    }
    else {
      if (ttHit)
        ss->staticEval = eval = ttEntry->getStaticEval();
      else
        ss->staticEval = eval = Eval::evaluate();

      if (ttFlag & flagForTT(ttValue > eval))
        ss->staticEval = eval = ttValue;
    }

    if ((ss - 2)->staticEval != VALUE_NONE)
      improving = ss->staticEval > (ss - 2)->staticEval;
    else if ((ss - 4)->staticEval != VALUE_NONE)
      improving = ss->staticEval > (ss - 4)->staticEval;

    // depth should always be >= 1 at this point

    // Razoring
    if (eval < alpha - 400 * depth) {
      Value value = qsearch<NonPV>(alpha-1, alpha, ss);
      if (value < alpha)
        return value;
    }

    // Reverse futility pruning
    if (!PvNode
      && depth < 9
      && abs(eval) < VALUE_TB_WIN_IN_MAX_PLY
      && eval >= beta
      && eval + 120 * improving - 140 * depth >= beta)
      return eval;

    // Null move pruning
    if (!PvNode
      && !excludedMove
      && (ss - 1)->playedMove != MOVE_NONE
      && eval >= beta
      && position.hasNonPawns(position.sideToMove)
      && beta > VALUE_TB_LOSS_IN_MAX_PLY) {

      int R = std::min((eval - beta) / 200, 3) + depth / 3 + 4;

      playNullMove(ss);
      Value nullValue = -negaMax<NonPV>(-beta, -beta + 1, depth - R, !cutNode, ss + 1);
      cancelMove();

      if (nullValue >= beta && abs(nullValue) < VALUE_TB_WIN_IN_MAX_PLY) {
        return nullValue;
      }
    }

    // IIR
    if (cutNode && depth >= 4 && !ttMove)
      depth -= 2;

  moves_loop:

    const bool wasInCheck = position.checkers;

    MoveList moves;
    if (rootNode) {
      moves = rootMoves;

      for (int i = 0; i < rootMoves.size(); i++)
        rootMoves.scores[i] = -VALUE_INFINITE;
    }
    else {
      getPseudoLegalMoves(position, &moves);
      scoreMoves(moves, ttMove, ss);
    }

    bool foundLegalMove = false;

    int playedMoves = 0;

    Move quietMoves[64];
    int quietCount = 0;

    bool skipQuiets = false;

    for (int i = 0; i < moves.size(); i++) {
      Move move = nextBestMove(moves, i);

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

      foundLegalMove = true;

      if (!rootNode
        && position.hasNonPawns(us)
        && bestValue > VALUE_TB_LOSS_IN_MAX_PLY)
      {
        if (quietCount > (3 * depth * depth + 9) / (2 - improving))
          skipQuiets = true;

        if (pieceOn(getMoveDest(move))) {
          if (!position.see_ge(move, Value(-140 * depth)))
            continue;
        }
      }

      int extension = 0;
      
      if ( !rootNode
        && ply < 2 * rootDepth
        && depth >= 6
        && !excludedMove
        && move == ttMove
        && abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY
        && ttFlag & TT::FLAG_LOWER
        && ttEntry->getDepth() >= depth - 3) 
      {
        Value singularBeta = ttValue - depth;
        
        ss->excludedMove = move;
        Value seValue = negaMax<NonPV>(singularBeta - 1, singularBeta, (depth - 1) / 2, cutNode, ss);
        ss->excludedMove = MOVE_NONE;
        
        if (seValue < singularBeta)
          extension = 1;
        else if (singularBeta >= beta)
          return singularBeta;
      }

      playMove(move, ss);

      int newDepth = depth + extension - 1;

      Value value;

      bool needFullSearch;
      if (!wasInCheck && depth >= 3 && playedMoves > (1 + 2 * PvNode)) {
        int R = lmrTable[depth][playedMoves + 1];

        R += !improving;

        R -= PvNode;

        R += cutNode;

        // Do the clamp to avoid a qsearch or an extension in the child search
        int reducedDepth = std::clamp(newDepth - R, 1, newDepth + 1);

        value = -negaMax<NonPV>(-alpha - 1, -alpha, reducedDepth, true, ss + 1);

        needFullSearch = value > alpha && reducedDepth < newDepth;
      }
      else
        needFullSearch = !PvNode || playedMoves >= 1;


      if (needFullSearch)
        value = -negaMax<NonPV>(-alpha - 1, -alpha, newDepth, !cutNode, ss + 1);

      if (PvNode && (playedMoves == 0 || value > alpha))
        value = -negaMax<PV>(-beta, -alpha, newDepth, false, ss + 1);

      cancelMove();

      playedMoves++;

      if (rootNode)
        rootMoves.scores[rootMoves.indexOf(move)] = value;

      if (value > bestValue) {
        bestValue = value;

        if (bestValue > alpha) {
          bestMove = move;

          if (PvNode)
            updatePV(ss, bestMove);

          // Always true in NonPV nodes
          if (bestValue >= beta)
            break;

          alpha = bestValue;
        }
      }
    }

    if (!foundLegalMove) {
      if (excludedMove) 
        return alpha;

      return position.checkers ? Value(ply - VALUE_MATE) : VALUE_DRAW;
    }

    // Update histories
    if (bestValue >= beta &&
      position.isQuiet(bestMove))
    {
      int bonus = (bestValue > beta + 110) ? stat_bonus(depth + 1) : stat_bonus(depth);

      // Butterfly history
      addToHistory(mainHistory[position.sideToMove][fromTo(bestMove)], bonus);

      // Continuation history
      if ((ss - 1)->playedMove)
        addToHistory((ss - 1)->contHistory()[pieceTo(bestMove)], bonus);
      if ((ss - 2)->playedMove)
        addToHistory((ss - 2)->contHistory()[pieceTo(bestMove)], bonus);

      // Decrease score of other quiet moves
      for (int i = 0; i < quietCount; i++) {
        Move otherMove = quietMoves[i];
        if (otherMove == bestMove)
          continue;

        if ((ss - 1)->playedMove)
          addToHistory((ss - 1)->contHistory()[pieceTo(otherMove)], -bonus);
        if ((ss - 2)->playedMove)
          addToHistory((ss - 2)->contHistory()[pieceTo(otherMove)], -bonus);

        addToHistory(mainHistory[position.sideToMove][fromTo(otherMove)], -bonus);
      }

      // Counter-move history
      if ((ss - 1)->playedMove) {
        Square prevSq = getMoveDest((ss - 1)->playedMove);
        counterMoveHistory[pieceOn(prevSq)][prevSq] = bestMove;
      }

      // Killers
      if (bestMove != ss->killers[0]) {
        ss->killers[1] = ss->killers[0];
        ss->killers[0] = bestMove;
      }
    }

    // Store to TT
    if (!excludedMove) {
      TT::Flag flag;
      if (bestValue >= beta)
        flag = TT::FLAG_LOWER;
      else
        flag = (PvNode && bestMove) ? TT::FLAG_EXACT : TT::FLAG_UPPER;

      ttEntry->store(position.key, flag, depth, bestMove, bestValue, ss->staticEval);
    }

    return bestValue;
  }


  std::string getPvString(SearchInfo* ss) {

    ostringstream output;

    for (int i = 0; i < ss->pvLength; i++) {
      Move move = ss->pv[i];
      if (!move)
        break;

      output << UCI::move(move) << ' ';
    }

    return output.str();
  }

  constexpr int SsOffset = 4;

  SearchInfo searchStack[MAX_PLY + SsOffset];

  void startSearch() {

    Move bestMove;

    clock_t optimumTime;

    if (searchLimits.hasTimeLimit())
      optimumTime = TimeMan::calcOptimumTime(searchLimits, position.sideToMove);

    ply = 0;

    nodesSearched = 0;

    rootColor = position.sideToMove;

    SearchLoopInfo iterDeepening[MAX_PLY];

    for (int i = 0; i < MAX_PLY + SsOffset; i++) {
      searchStack[i].staticEval = VALUE_NONE;

      searchStack[i].pvLength = 0;

      searchStack[i].killers[0] = MOVE_NONE;
      searchStack[i].killers[1] = MOVE_NONE;

      searchStack[i].excludedMove = MOVE_NONE;
    }

    SearchInfo* ss = &searchStack[SsOffset];

    if (searchLimits.depth == 0)
      searchLimits.depth = MAX_PLY;

    // Setup root moves
    rootMoves = MoveList();
    {
      MoveList pseudoRootMoves;
      getPseudoLegalMoves(position, &pseudoRootMoves);

      for (int i = 0; i < pseudoRootMoves.size(); i++) {
        Move move = pseudoRootMoves[i];
        if (!position.isLegal(move))
          continue;

        rootMoves.add(move);
      }
    }
    scoreMoves(rootMoves, MOVE_NONE, ss);

    clock_t startTime = timeMillis();

    int searchStability = 0;

    for (rootDepth = 1; rootDepth <= searchLimits.depth; rootDepth++) {

      if (searchLimits.nodes && nodesSearched >= searchLimits.nodes)
        break;

      selDepth = 0;

      Value score;
      if (rootDepth >= 4) {
        int windowSize = 10;
        Value alpha = iterDeepening[rootDepth - 1].score - windowSize;
        Value beta = iterDeepening[rootDepth - 1].score + windowSize;

        int failedHighCnt = 0;
        while (true) {

          int adjustedDepth = std::max(1, rootDepth - failedHighCnt);

          score = negaMax<Root>(alpha, beta, adjustedDepth, false, ss);

          if (Threads::searchState == STOP_PENDING)
            goto bestMoveDecided;

          if (searchLimits.nodes && nodesSearched >= searchLimits.nodes)
            break; // only break, in order to print info about the partial search we've done

          if (score >= VALUE_MATE_IN_MAX_PLY) {
            beta = VALUE_INFINITE;
            failedHighCnt = 0;
          }

          if (score <= alpha) {
            beta = Value((alpha + beta) / 2);
            alpha = (Value)std::max(-VALUE_INFINITE, alpha - windowSize);

            failedHighCnt = 0;
          }
          else if (score >= beta) {
            beta = (Value)std::min(VALUE_INFINITE, beta + windowSize);

            failedHighCnt = std::min(11, failedHighCnt + 1);
          }
          else
            break;

          windowSize += windowSize / 3;
        }
      }
      else {
        score = negaMax<Root>(-VALUE_INFINITE, VALUE_INFINITE, rootDepth, false, ss);
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
          << " score " << UCI::value(score)
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
      if (score >= VALUE_MATE_IN_MAX_PLY)
        goto bestMoveDecided;

      if (searchLimits.hasTimeLimit() && rootDepth >= 4) {

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
    lastSearchTimeSpan = timeMillis() - startTime;

    if (printingEnabled)
      std::cout << "bestmove " << UCI::move(bestMove) << endl;

    Threads::searchState = STOPPED;
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