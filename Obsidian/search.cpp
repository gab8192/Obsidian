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

void FinnyEntry::reset() {
  memset(byColorBB, 0, sizeof(byColorBB));
  memset(byPieceBB, 0, sizeof(byPieceBB));
  acc.reset(WHITE);
  acc.reset(BLACK);
}

namespace Search {

  DEFINE_PARAM_S(MpPvsSeeMargin, -80, 15);
  DEFINE_PARAM_S(MpQsSeeMargin, -25, 15);

  DEFINE_PARAM_S(LmrBase, 39, 10);
  DEFINE_PARAM_S(LmrDiv, 211, 10);

  DEFINE_PARAM_S(StatBonusLinear, 130, 10);
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
  
  bool doingBench = false;

  int lmrTable[MAX_PLY][MAX_MOVES];

  Settings::Settings() {
    time[WHITE] = time[BLACK] = inc[WHITE] = inc[BLACK] = movetime = 0;
    movestogo = 0;
    depth = MAX_PLY-4; // no depth limit by default
    nodes = 0;
  }

  Move moveFromTbProbeRoot(Position& pos, unsigned tbResult) {

    constexpr PieceType PROMO_TYPE_TABLE[5] = {
      NO_PIECE_TYPE,
      QUEEN,
      ROOK,
      BISHOP,
      KNIGHT
    };

    const Square from = (Square) TB_GET_FROM(tbResult);
    const Square to = (Square) TB_GET_TO(tbResult);
    const PieceType promoType = PROMO_TYPE_TABLE[TB_GET_PROMOTES(tbResult)];

    if (promoType)
      return createPromoMove(from, to, promoType);

    if (TB_GET_EP(tbResult))
      return createMove(from, to, MT_EN_PASSANT);

    return createMove(from, to, MT_NORMAL);
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
  void init() {
    initLmrTable();
  }

  void Thread::resetHistories() {
    memset(mainHistory, 0, sizeof(mainHistory));
    memset(captureHistory, 0, sizeof(captureHistory));
    memset(counterMoveHistory, 0, sizeof(counterMoveHistory));
    memset(contHistory, 0, sizeof(contHistory));
  }

  Thread::Thread() :
    thread(std::thread(&Thread::idleLoop, this))
  {
    resetHistories();
  }

  template<bool root>
  int64_t perft(Position& pos, int depth) {

    MoveList moves;
    getStageMoves(pos, ADD_ALL_MOVES, &moves);

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
    return timeMillis() - Threads::getSearchSettings().startTime;
  }

  int stat_bonus(int d) {
    return std::min(StatBonusLinear * d, (int)StatBonusMax);
  }

  void Thread::sortRootMoves(int offset) {
    for (int i = offset; i < rootMoves.size(); i++) {
      int best = i;

      for (int j = i + 1; j < rootMoves.size(); j++)
        if (rootMoves[j].score > rootMoves[best].score)
          best = j;

      if (best != i)
        std::swap(rootMoves[i], rootMoves[best]);
    }
  }

  bool Thread::visitRootMove(Move move) {
    for (int i = pvIdx; i < rootMoves.size(); i++) {
      if (move == rootMoves[i].move)
        return true;
    }
    return false;
  }

  bool Thread::usedMostOfTime() {

    if (Threads::getSearchSettings().hasTimeLimit())
      return elapsedTime() >= maxTime;
    
    else if (Threads::getSearchSettings().movetime) {
      clock_t timeLimit = Threads::getSearchSettings().movetime;
      return elapsedTime() >= (timeLimit - 50);
    }

    return false;
  }

  void Thread::playNullMove(Position& pos, SearchInfo* ss) {
    TT::prefetch(pos.key ^ ZOBRIST_TEMPO);
    nodesSearched++;

    ss->contHistory = contHistory[false][0];
    ss->playedMove = MOVE_NONE;
    keyStack[keyStackHead++] = pos.key;

    ply++;
    pos.doNullMove();
  }

  void Thread::cancelNullMove() {
    ply--;
    keyStackHead--;
  }

  void Thread::refreshAccumulator(Position& pos, NNUE::Accumulator& acc, Color side) {
    const Square king = pos.kingSquare(side);
    const int bucket = NNUE::KingBucketsScheme[relative_square(side, king)];
    FinnyEntry& entry = finny[fileOf(king) >= FILE_E][bucket];

    for (Color c = WHITE; c <= BLACK; ++c) {
      for (PieceType pt = PAWN; pt <= KING; ++pt) {
        const Bitboard oldBB = entry.byColorBB[side][c] & entry.byPieceBB[side][pt];
        const Bitboard newBB = pos.pieces(c, pt);
        Bitboard toRemove = oldBB & ~newBB;
        Bitboard toAdd = newBB & ~oldBB;

        while (toRemove) {
          Square sq = popLsb(toRemove);
          entry.acc.removePiece(king, side, makePiece(c, pt), sq);
        }
        while (toAdd) {
          Square sq = popLsb(toAdd);
          entry.acc.addPiece(king, side, makePiece(c, pt), sq);
        }
      }
    }

    memcpy(acc.colors[side], entry.acc.colors[side], sizeof(acc.colors[0]));
    memcpy(entry.byColorBB[side], pos.byColorBB, sizeof(entry.byColorBB[0]));
    memcpy(entry.byPieceBB[side], pos.byPieceBB, sizeof(entry.byPieceBB[0]));
  }

  void Thread::playMove(Position& pos, Move move, SearchInfo* ss) {

    nodesSearched++;

    const bool isCap = pos.board[move_to(move)] != NO_PIECE;
    ss->contHistory = contHistory[isCap][pieceTo(pos, move)];
    ss->playedMove = move;
    keyStack[keyStackHead++] = pos.key;

    Square oldKingSquares[COLOR_NB];
    oldKingSquares[WHITE] = pos.kingSquare(WHITE);
    oldKingSquares[BLACK] = pos.kingSquare(BLACK);

    NNUE::Accumulator& oldAcc = accumStack[accumStackHead];
    NNUE::Accumulator& newAcc = accumStack[++accumStackHead];

    DirtyPieces dirtyPieces;

    ply++;
    pos.doMove(move, dirtyPieces);

    TT::prefetch(pos.key);

    for (Color side = WHITE; side <= BLACK; ++side) {
      if (NNUE::needRefresh(side, oldKingSquares[side], pos.kingSquare(side)))
        refreshAccumulator(pos, newAcc, side);
      else
        newAcc.doUpdates(pos.kingSquare(side), side, dirtyPieces, oldAcc); 
    }
  }

  void Thread::cancelMove() {
    ply--;
    keyStackHead--;
    accumStackHead--;
  }

  int Thread::getCapHistory(Position& pos, Move move) {
    PieceType captured = piece_type(pos.board[move_to(move)]);
    return captureHistory[pieceTo(pos, move)][captured];
  }

  int Thread::getQuietHistory(Position& pos, Move move, SearchInfo* ss) {
    int chIndex = pieceTo(pos, move);
    return    mainHistory[pos.sideToMove][move_from_to(move)]
            + (ss - 1)->contHistory[chIndex]
            + (ss - 2)->contHistory[chIndex]
            + (ss - 4)->contHistory[chIndex];
  }

  void addToContHistory(Position& pos, int bonus, Move move, SearchInfo* ss) {
    int chIndex = pieceTo(pos, move);
    if ((ss - 1)->playedMove)
      addToHistory((ss - 1)->contHistory[chIndex], bonus);
    if ((ss - 2)->playedMove)              
      addToHistory((ss - 2)->contHistory[chIndex], bonus);
    if ((ss - 4)->playedMove)              
      addToHistory((ss - 4)->contHistory[chIndex], bonus);
  }

  void Thread::updateHistories(Position& pos, int bonus, Move bestMove, Score bestScore,
                       Score beta, Move* quiets, int quietCount, SearchInfo* ss) {
    // Butterfly history
    addToHistory(mainHistory[pos.sideToMove][move_from_to(bestMove)], bonus);

    // Continuation history
    addToContHistory(pos, bonus, bestMove, ss);

    // Decrease score of other quiet moves
    for (int i = 0; i < quietCount; i++) {
      Move otherMove = quiets[i];
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

  TT::Flag boundForTT(bool failsHigh) {
    return failsHigh ? TT::FLAG_LOWER : TT::FLAG_UPPER;
  }

  bool Thread::hasUpcomingRepetition(Position& pos, int ply) {

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
      if ((BETWEEN_BB[from][to] ^ to) & occ)
        continue;

      // Repetition after root
      if (ply > i)
        return true;
      
      Piece pc = pos.board[ pos.board[from] ? from : to ];

      if (piece_color(pc) != pos.sideToMove)
        continue;

      // We want one more repetition before root
      for (int j = i+4; j <= maxDist; j += 2) {
        if (keyStack[keyStackHead - j] == keyStack[keyStackHead - i])
          return true;
      }
    }

    return false;
  }

  bool Thread::isRepetition(Position& pos, int ply) {

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

  Score Thread::makeDrawScore() {
    return int(nodesSearched & 2) - 1;
  }

  template<bool IsPV>
  Score Thread::qsearch(Position& pos, Score alpha, Score beta, SearchInfo* ss) {
    
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
    bool ttPV = false;

    if (ttHit) {
      ttBound = ttEntry->getBound();
      ttScore = ttEntry->getScore(ply);
      ttMove = ttEntry->getMove();
      ttStaticEval = ttEntry->getStaticEval();
      ttPV = ttEntry->wasPV();
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
      MovePicker::QSEARCH, pos,
      visitTTMove ? ttMove : MOVE_NONE,
      MOVE_NONE, MOVE_NONE,
      mainHistory, captureHistory,
      MpQsSeeMargin,
      ss);

    bool foundLegalMoves = false;

    // Visit moves
    MovePicker::Stage moveStage;
    Move move;

    while (move = movePicker.nextMove(&moveStage)) {

      if (bestScore > SCORE_TB_LOSS_IN_MAX_PLY) {
        // Prevent qsearch from visiting bad captures and under-promotions
        if (moveStage > MovePicker::PLAY_QUIETS)
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

      if (bestScore > SCORE_TB_LOSS_IN_MAX_PLY) {
        // This implies that we are in check too
        if (moveStage == MovePicker::PLAY_QUIETS)
          break;
      }
    }

    if (pos.checkers && !foundLegalMoves)
      return ply - SCORE_MATE;

    ttEntry->store(pos.key,
      bestScore >= beta ? TT::FLAG_LOWER : TT::FLAG_UPPER,
      0, bestMove, bestScore, ss->staticEval, ttPV, ply);

    return bestScore;
  }

  void updatePV(SearchInfo* ss, int ply, Move move) {

    ss->pvLength = (ss + 1)->pvLength;

    // set the move in the pv
    ss->pv[ply] = move;

    // copy all the moves that follow, from the child pv
    for (int i = ply + 1; i < (ss + 1)->pvLength; i++)
      ss->pv[i] = (ss + 1)->pv[i];
  }

  TbResult probeTB(Position& pos) {
    if (BitCount(pos.pieces()) > TB_LARGEST)
        return TB_RESULT_FAILED;

    return tb_probe_wdl(
      pos.pieces(WHITE), pos.pieces(BLACK),
      pos.pieces(KING), pos.pieces(QUEEN), pos.pieces(ROOK),
      pos.pieces(BISHOP), pos.pieces(KNIGHT), pos.pieces(PAWN),
      pos.halfMoveClock, 
      pos.castlingRights, 
      pos.epSquare == SQ_NONE ? 0 : pos.epSquare,
      pos.sideToMove == WHITE);
  }

  template<bool IsPV>
  Score Thread::negamax(Position& pos, Score alpha, Score beta, int depth, bool cutNode, SearchInfo* ss) {

    const bool IsRoot = IsPV && ply == 0;

    // Check time
    ++maxTimeCounter;
    if ( this == Threads::mainThread() 
      && (maxTimeCounter & 16383) == 0
      && usedMostOfTime())
        Threads::stopSearch();

    if (Threads::isSearchStopped())
      return SCORE_DRAW;
    
    // Init node
    if (IsPV)
      ss->pvLength = ply;

    // Detect upcoming draw
    if (!IsRoot && alpha < SCORE_DRAW && hasUpcomingRepetition(pos, ply)) {
      alpha = makeDrawScore();
      if (alpha >= beta)
        return alpha;
    }

    // Enter qsearch when depth is 0
    if (depth <= 0)
      return qsearch<IsPV>(pos, alpha, beta, ss);

    // Detect draw
    if (!IsRoot && (isRepetition(pos, ply) || pos.halfMoveClock >= 100))
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

    if (IsRoot)
      ttMove = rootMoves[pvIdx].move;

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
    const TbResult tbResult = (IsRoot || excludedMove) ? TB_RESULT_FAILED : probeTB(pos);

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

    // At root we always assume improving, for lmr purposes
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
      Score score = -negamax<false>(newPos, -beta, -beta + 1, depth - R, !cutNode, ss + 1);
      cancelNullMove();

      if (score >= beta)
        return score < SCORE_TB_WIN_IN_MAX_PLY ? score : beta;
    }

    // IIR. Decrement the depth if we expect this search to have bad move ordering
    if ((IsPV || cutNode) && depth >= 2+2*cutNode && !ttMove)
      depth--;

    if (   !IsPV
        && depth >= 5
        && std::abs(beta) < SCORE_TB_WIN_IN_MAX_PLY
        && !(ttDepth >= depth - 3 && ttScore < probcutBeta))
    {
      int pcSeeMargin = (probcutBeta - ss->staticEval) * 10 / 16;
      bool visitTTMove = ttMove && !pos.isQuiet(ttMove) && pos.seeGe(ttMove, pcSeeMargin);

      MovePicker pcMovePicker(
        MovePicker::PROBCUT, pos,
        visitTTMove ? ttMove : MOVE_NONE, MOVE_NONE, MOVE_NONE,
        mainHistory, captureHistory,
        pcSeeMargin,
        ss);

      Move move;
      MovePicker::Stage moveStage;

      while (move = pcMovePicker.nextMove(&moveStage)) {
        if (!pos.isLegal(move))
          continue;

        Position newPos = pos;
        playMove(newPos, move, ss);

        Score score = -qsearch<false>(newPos, -probcutBeta, -probcutBeta + 1, ss + 1);

        // Do a normal search if qsearch was positive
        if (score >= probcutBeta)
          score = -negamax<false>(newPos, -probcutBeta, -probcutBeta + 1, depth - 4, !cutNode, ss + 1);

        cancelMove();

        if (score >= probcutBeta)
          return score;
      }
    }

  moves_loop:

    // Generate moves and score them

    int seenMoves = 0;

    Move quiets[64];
    int quietCount = 0;
    Move captures[64];
    int captureCount = 0;

    Move counterMove = MOVE_NONE;
    if ((ss - 1)->playedMove) {
      Square prevSq = move_to((ss - 1)->playedMove);
      counterMove = counterMoveHistory[pos.board[prevSq] * SQUARE_NB + prevSq];
    }

    if (IsRoot)
      ss->killerMove = MOVE_NONE;

    MovePicker movePicker(
      MovePicker::PVS, pos,
      ttMove, ss->killerMove, counterMove,
      mainHistory, captureHistory,
      MpPvsSeeMargin,
      ss);

    // Visit moves

    Move move;
    MovePicker::Stage moveStage;

    while (move = movePicker.nextMove(& moveStage)) {
      if (move == excludedMove)
        continue;

      if (!pos.isLegal(move))
        continue;

      if (IsRoot && !visitRootMove(move))
        continue;
      
      seenMoves++;
      
      bool isQuiet = pos.isQuiet(move);

      int history = isQuiet ? getQuietHistory(pos, move, ss) : getCapHistory(pos, move);

      int oldNodesSearched = nodesSearched;

      if ( !IsRoot
        && bestScore > SCORE_TB_LOSS_IN_MAX_PLY
        && pos.hasNonPawns(pos.sideToMove))
      {
        // SEE (Static Exchange Evalution) pruning
        if (moveStage > MovePicker::PLAY_GOOD_CAPTURES) {
          int seeMargin = depth * (isQuiet ? PvsQuietSeeMargin : PvsCapSeeMargin);
          if (!pos.seeGe(move, seeMargin))
            continue;
        }

        if (isQuiet) {
          // Late move pruning. At low depths, only visit a few quiet moves
          if (seenMoves >= (depth * depth + LmpBase) / (2 - improving))
            movePicker.stage = MovePicker::PLAY_BAD_CAPTURES;

          int lmrRed = lmrTable[depth][seenMoves] + !improving - history / EarlyLmrHistoryDiv;
          int lmrDepth = std::max(0, depth - lmrRed);

          // Futility pruning. If our evaluation is far below alpha,
          // only visit a few quiet moves
          if (   lmrDepth <= FpMaxDepth 
              && !pos.checkers 
              && ss->staticEval + FpBase + FpDepthMul * lmrDepth <= alpha)
            movePicker.stage = MovePicker::PLAY_BAD_CAPTURES;
        }
      }

      int extension = 0;
      
      // Singular extension
      if ( !IsRoot
        && ply < 2 * rootDepth
        && depth >= 6
        && !excludedMove
        && move == ttMove
        && abs(ttScore) < SCORE_TB_WIN_IN_MAX_PLY
        && ttBound & TT::FLAG_LOWER
        && ttDepth >= depth - 3) 
      {
        Score singularBeta = ttScore - depth;
        
        ss->excludedMove = move;
        Score seScore = negamax<false>(pos, singularBeta - 1, singularBeta, (depth - 1) / 2, cutNode, ss);
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

      if (depth >= 2 && seenMoves > 1 + 3 * IsRoot) {

        int R = lmrTable[depth][seenMoves] / (1 + !isQuiet);

        // Reduce or extend depending on history of this move
        R -= history / (isQuiet ? LmrQuietHistoryDiv : LmrCapHistoryDiv);

        // Extend moves that give check
        R -= (newPos.checkers != 0ULL);

        // Extend if this position *was* in a PV node. Even further if it *is*
        R -= ttPV + IsPV;

        // Extend if this move is killer or counter
        R -= (   moveStage == MovePicker::PLAY_KILLER 
              || moveStage == MovePicker::PLAY_COUNTER);

        // Reduce more if the expected best move is a capture
        R += ttMoveNoisy;

        // Reduce if evaluation is trending down
        R += !improving;

        // Reduce if we expect to fail high
        R += 2 * cutNode;

        // Clamp to avoid a qsearch or an extension in the child search
        int reducedDepth = std::clamp(newDepth - R, 1, newDepth + 1);

        score = -negamax<false>(newPos, -alpha - 1, -alpha, reducedDepth, true, ss + 1);

        if (score > alpha && reducedDepth < newDepth) {
          newDepth += (score > bestScore + ZwsDeeperMargin && !IsRoot);
          newDepth -= (score < bestScore + newDepth        && !IsRoot);
          needFullSearch = reducedDepth < newDepth;
        }
      }
      else
        needFullSearch = !IsPV || seenMoves > 1;

      if (needFullSearch)
        score = -negamax<false>(newPos, -alpha - 1, -alpha, newDepth, !cutNode, ss + 1);

      if (IsPV && (seenMoves == 1 || score > alpha))
        score = -negamax<true>(newPos, -beta, -alpha, newDepth, false, ss + 1);

      cancelMove();

      if (Threads::isSearchStopped())
        return SCORE_DRAW;

      if (IsRoot) {
        RootMove& rm = rootMoves[rootMoves.indexOf(move)];
        rm.nodes += nodesSearched - oldNodesSearched;

        if (seenMoves == 1 || score > alpha) {
          rm.score = score;

          rm.pvLength = (ss+1)->pvLength;
          rm.pv[0] = move;
          for (int i = 1; i < (ss+1)->pvLength; i++)
            rm.pv[i] = (ss+1)->pv[i];
        }
        else // this move gave an upper bound, so we don't know how to sort it
          rm.score = - SCORE_INFINITE; 
      }

      if (score > bestScore) {
        bestScore = score;

        if (bestScore > alpha) {
          bestMove = move;

          if (IsPV && !IsRoot)
            updatePV(ss, ply, bestMove);

          // Always true in NonPV nodes
          if (bestScore >= beta)
            break;

          alpha = bestScore;
        }
      }

      // Register the move to decrease its history later. Unless it raised alpha
      if (move != bestMove) {
        if (isQuiet) {
          if (quietCount < 64)
            quiets[quietCount++] = move;
        }
        else {
          if (captureCount < 64)
            captures[captureCount++] = move;
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
        PieceType captured = piece_type(pos.board[move_to(bestMove)]);
        addToHistory(captureHistory[pieceTo(pos, bestMove)][captured], bonus);
      }

      for (int i = 0; i < captureCount; i++) {
        Move otherMove = captures[i];
        PieceType captured = piece_type(pos.board[move_to(otherMove)]);
        addToHistory(captureHistory[pieceTo(pos, otherMove)][captured], -bonus);
      }
    }

    // Only in pv nodes we could probe tt and not cut off immediately
    if (IsPV)
      bestScore = std::min(bestScore, maxScore);

    // Store to TT
    if (!excludedMove && !(IsRoot && pvIdx > 0)) {
      TT::Flag flag;
      if (bestScore >= beta)
        flag = TT::FLAG_LOWER;
      else
        flag = (IsPV && bestMove) ? TT::FLAG_EXACT : TT::FLAG_UPPER;

      ttEntry->store(pos.key, flag, depth, bestMove, bestScore, ss->staticEval, ttPV, ply);
    }

    return bestScore;
  }

  std::string getPvString(RootMove& rm) {

    std::ostringstream output;

    output << UCI::moveToString(rm.move);

    for (int i = 1; i < rm.pvLength; i++) {
      Move move = rm.pv[i];
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

  void Thread::startSearch() {

    const Settings& settings = Threads::getSearchSettings();

    Position rootPos = settings.position;

    accumStackHead = 0;
    accumStack[0].refresh(rootPos, WHITE);
    accumStack[0].refresh(rootPos, BLACK);
    
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < NNUE::KingBucketsCount; j++)
          finny[i][j].reset();

    keyStackHead = 0;
    for (int i = 0; i < settings.prevPositions.size(); i++)
      keyStack[keyStackHead++] = settings.prevPositions[i];

    if (settings.hasTimeLimit())
      TimeMan::calcOptimumTime(settings, rootPos.sideToMove,
                              &optimumTime, &maxTime);

    ply = 0;
    tbHits = 0;
    nodesSearched = 0;
    maxTimeCounter = 0;

    SearchLoopInfo idStack[MAX_PLY];

    for (int i = 0; i < MAX_PLY + SsOffset; i++) {
      searchStack[i].staticEval = SCORE_NONE;

      searchStack[i].pvLength = 0;

      searchStack[i].killerMove   = MOVE_NONE;
      searchStack[i].excludedMove = MOVE_NONE;
      searchStack[i].playedMove   = MOVE_NONE;

      searchStack[i].contHistory = contHistory[false][0];

      searchStack[i].doubleExt = 0;
    }

    int searchStability = 0;

    SearchInfo* ss = &searchStack[SsOffset];

    clock_t startTimeForBench = timeMillis();

    // Setup root moves
    rootMoves = RootMoveList();
    {
      MoveList pseudoRootMoves;
      getStageMoves(rootPos, ADD_ALL_MOVES, &pseudoRootMoves);

      for (int i = 0; i < pseudoRootMoves.size(); i++) {
        Move move = pseudoRootMoves[i].move;
        if (rootPos.isLegal(move))
          rootMoves.add(move);
      }
    }

    if ( this == Threads::mainThread()
      && BitCount(rootPos.pieces()) <= TB_LARGEST) {

      unsigned result = tb_probe_root(
          rootPos.pieces(WHITE), rootPos.pieces(BLACK),
          rootPos.pieces(KING), rootPos.pieces(QUEEN), rootPos.pieces(ROOK),
          rootPos.pieces(BISHOP), rootPos.pieces(KNIGHT), rootPos.pieces(PAWN),
          rootPos.halfMoveClock, 
          rootPos.castlingRights, 
          rootPos.epSquare == SQ_NONE ? 0 : rootPos.epSquare,
          rootPos.sideToMove == WHITE,
          nullptr);

      if (result != TB_RESULT_FAILED) {
        Move tbBestMove = moveFromTbProbeRoot(rootPos, result);

        // Clear all the root moves, and set the only root move as the tablebases best move
        // For analysis purposes, we don't want to instantly just print the move (though we could)
        rootMoves = RootMoveList();
        rootMoves.add(tbBestMove);
      }
    }

    // Search starting. Zero out the nodes of each root move
    for (int i = 0; i < rootMoves.size(); i++)
      rootMoves[i].nodes = 0;

    const int multiPV = std::min(int(Options["MultiPV"]), rootMoves.size());

    for (rootDepth = 1; rootDepth <= settings.depth; rootDepth++) {

      // Only one legal move? For analysis purposes search, but with a limited depth
      if (rootDepth > 10 && rootMoves.size() == 1)
        break;

      for (pvIdx = 0; pvIdx < multiPV; pvIdx++) {
        int window = AspWindowStartDelta;
        Score alpha = -SCORE_INFINITE;
        Score beta  = SCORE_INFINITE;
        int failHighCount = 0;

        if (rootDepth >= AspWindowStartDepth) {
          alpha = std::max(-SCORE_INFINITE, rootMoves[pvIdx].score - window);
          beta  = std::min( SCORE_INFINITE, rootMoves[pvIdx].score + window);
        }

        while (true) {

          if (beta >= 4000) {
            beta = SCORE_INFINITE;
            failHighCount = 0;
          }

          int adjustedDepth = std::max(1, rootDepth - failHighCount);

          Score score = negamax<true>(rootPos, alpha, beta, adjustedDepth, false, ss);

          // Discard any result if search was abruptly stopped
          if (Threads::isSearchStopped())
            goto bestMoveDecided;

          if ( rootDepth > 1 
            && settings.nodes
            && Threads::totalNodes() > settings.nodes)
            goto bestMoveDecided;

          sortRootMoves(pvIdx);

          if (score <= alpha) {
            beta = (alpha + beta) / 2;
            alpha = std::max(-SCORE_INFINITE, alpha - window);

            failHighCount = 0;
          }
          else if (score >= beta) {
            beta = std::min(SCORE_INFINITE, beta + window);

            failHighCount++;
          }
          else
            break;

          window += window / 3;
        }

        sortRootMoves(0);
      }

      const Move bestMove = rootMoves[0].move;
      const Score score = rootMoves[0].score;

      idStack[rootDepth].score = score;
      idStack[rootDepth].bestMove = bestMove;

      if (this != Threads::mainThread())
        continue;

      const clock_t elapsed = elapsedTime();
      const clock_t elapsedStrict = timeMillis() - startTimeForBench;

      for (int i = 0; i < multiPV; i++) {
        if (doingBench)
          break; // save indentation

        std::ostringstream infoStr;
        infoStr
          << "info"
          << " depth "   << rootDepth
          << " multipv " << (i+1)
          << " score "   << UCI::scoreToString(rootMoves[i].score)
          << " nodes "   << Threads::totalNodes()
          << " nps "     << (Threads::totalNodes() * 1000ULL) / std::max(elapsedStrict, 1L)
          << " tbhits "  << Threads::totalTbHits()
          << " time "    << elapsed
          << " pv "      << getPvString(rootMoves[i]);

        std::cout << infoStr.str() << std::endl;
      }

      // When playing in movetime mode, stop if we've used 75% time of movetime,
      // because we will probably not hit the next depth in time
      if (settings.movetime)
        if (elapsedTime() >= (settings.movetime * 3) / 4)
          goto bestMoveDecided;

      if (bestMove == idStack[rootDepth - 1].bestMove)
        searchStability = std::min(searchStability + 1, 8);
      else
        searchStability = 0;

      if (settings.hasTimeLimit() && rootDepth >= 4) {

        if (usedMostOfTime())
          goto bestMoveDecided;

        int bmNodes = rootMoves[rootMoves.indexOf(bestMove)].nodes;
        double notBestNodes = 1.0 - (bmNodes / double(nodesSearched));
        double nodesFactor     = (tm1/100.0) + notBestNodes * (tm0/100.0);

        double stabilityFactor = (tm2/100.0) - searchStability * (tm3/100.0);

        int scoreLoss = std::clamp<int>(idStack[rootDepth - 1].score - score, lol0, lol1);
        double scoreFactor     = (tm4/100.0) + scoreLoss * (tm5/1000.0);

        if (elapsed > stabilityFactor * nodesFactor * scoreFactor * optimumTime)
          goto bestMoveDecided;
      }
    }

  bestMoveDecided:

    // NOTE: When implementing best thread selection, don't mess up with tablebases dtz stuff

    if (this == Threads::mainThread() && !doingBench) {
      Threads::stopSearch();
      std::cout << "bestmove " << UCI::moveToString(rootMoves[0].move) << std::endl;
    }
  }

  void Thread::idleLoop() {
    while (true) {
      std::unique_lock lock(mutex);
      cv.wait(lock, [&] { return searching; });

      if (exitThread)
          return;

   // searching = true; (already done by the UCI thread)
      startSearch();
      searching = false;

      cv.notify_all();
    }
  }
}