#include "search.h"
#include "cuckoo.h"
#include "evaluate.h"
#include "movepick.h"
#include "fathom/src/tbprobe.h"
#include "timeman.h"
#include "threads.h"
#include "tt.h"
#include "tuning.h"
#include "uci.h"

#include <climits>
#include <cmath>
#include <sstream>
#include <unordered_map>

namespace Search {

  DEFINE_PARAM_S(QsFpMargin, 143, 14);

  DEFINE_PARAM_S(LmrBase, 100, 10);
  DEFINE_PARAM_S(LmrDiv, 287, 29);

  DEFINE_PARAM_S(PawnChWeight, 40, 5);
  DEFINE_PARAM_S(NonPawnChWeight, 50, 5);

  DEFINE_PARAM_S(StatBonusBias, -23, 50);
  DEFINE_PARAM_S(StatBonusLinear, 160, 15);
  DEFINE_PARAM_S(StatBonusMax, 1277, 100);
  DEFINE_PARAM_S(StatBonusBoostAt, 111, 10);

  DEFINE_PARAM_S(StatMalusBias, 17, 50);
  DEFINE_PARAM_S(StatMalusLinear, 186, 15);
  DEFINE_PARAM_S(StatMalusMax, 1127, 100);

  DEFINE_PARAM_S(EvalHistA, 58, 6);
  DEFINE_PARAM_S(EvalHistB, -494, 45);
  DEFINE_PARAM_S(EvalHistC, 509, 50);

  DEFINE_PARAM_S(RazoringDepthMul, 355, 38);

  DEFINE_PARAM_S(RfpMaxDepth, 11, 1);
  DEFINE_PARAM_S(RfpDepthMul, 85, 8);

  DEFINE_PARAM_S(NmpBase, 4, 1);
  DEFINE_PARAM_B(NmpDepthDiv, 3, 1, 21);
  DEFINE_PARAM_S(NmpEvalDiv, 151, 15);
  DEFINE_PARAM_S(NmpEvalDivMin, 4, 1);
  DEFINE_PARAM_S(NmpA, 27, 3);
  DEFINE_PARAM_S(NmpB, 205, 20);

  DEFINE_PARAM_S(ProbcutBetaMargin, 176, 18);

  DEFINE_PARAM_S(HistPrDepthMul, -4845, 460);

  DEFINE_PARAM_S(LmpBase,    3, 1);

  DEFINE_PARAM_S(QsSeeMargin, -32, 15);
  DEFINE_PARAM_S(PvsQuietSeeMargin, -35, 10);
  DEFINE_PARAM_S(PvsCapSeeMargin, -95, 10);

  DEFINE_PARAM_S(EarlyLmrHistoryDiv, 3489, 360);

  DEFINE_PARAM_S(FpBase, 160, 17);
  DEFINE_PARAM_S(FpMaxDepth, 10, 1);
  DEFINE_PARAM_S(FpDepthMul, 134, 13);

  DEFINE_PARAM_S(SBetaMargin, 64, 6);
  DEFINE_PARAM_S(TripleExtMargin, 121, 13);
  DEFINE_PARAM_S(DoubleExtMargin, 13, 1);

  DEFINE_PARAM_S(LmrQuietHistoryDiv, 8794, 860);
  DEFINE_PARAM_S(LmrCapHistoryDiv, 6319, 660);
  DEFINE_PARAM_S(ZwsDeeperMargin, 78, 8);

  DEFINE_PARAM_B(AspWindowStartDepth, 4, 4, 34);
  DEFINE_PARAM_B(AspWindowStartDelta, 11, 5, 25);

  int lmrTable[MAX_PLY][MAX_MOVES];

  Settings::Settings() {
    time[WHITE] = time[BLACK] = inc[WHITE] = inc[BLACK] = movetime = 0;
    movestogo = 0;
    depth = MAX_PLY-4; // no depth limit by default
    nodes = 0;
  }

  Move moveFromTbProbeRoot(unsigned tbResult) {

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
    memset(pawnCorrhist, 0, sizeof(pawnCorrhist));
    memset(wNonPawnCorrhist, 0, sizeof(wNonPawnCorrhist));
    memset(bNonPawnCorrhist, 0, sizeof(bNonPawnCorrhist));

    searchPrevScore = SCORE_NONE;
  }

  Thread::Thread()
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

  int64_t elapsedTime() {
    return timeMillis() - Threads::getSearchSettings().startTime;
  }

  void printInfo(int depth, int pvIdx, Score score, const std::string& pvString) {
    const int64_t elapsed = elapsedTime();
    std::ostringstream infoStr;
        infoStr
          << "info"
          << " depth "    << depth
          << " multipv "  << pvIdx
          << " score "    << UCI::scoreToString(score)
          << " nodes "    << Threads::totalNodes()
          << " nps "      << (Threads::totalNodes() * 1000ULL) / std::max<int64_t>(elapsed, 1LL)
          << " hashfull " << TT::hashfull()
          << " tbhits "   << Threads::totalTbHits()
          << " time "     << elapsed
          << " pv "       << pvString;

    std::cout << infoStr.str() << std::endl;
  }

  void printBestMove(Move move) {
    std::cout << "bestmove " << UCI::moveToString(move) << std::endl;
  }

  int statBonus(int d) {
    return std::min(StatBonusLinear * d + StatBonusBias, (int)StatBonusMax);
  }

  int statMalus(int d) {
    return std::min(StatMalusLinear * d + StatMalusBias, (int)StatMalusMax);
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

  void Thread::playNullMove(Position& pos, SearchInfo* ss) {
    ss->contHistory = contHistory[false][0];
    ss->playedMove = MOVE_NONE;
    ss->playedCap = false;
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
    NNUE::FinnyEntry& entry = finny[fileOf(king) >= FILE_E][bucket];

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
    acc.updated[side] = true;
    memcpy(acc.colors[side], entry.acc.colors[side], sizeof(acc.colors[0]));
    memcpy(entry.byColorBB[side], pos.byColorBB, sizeof(entry.byColorBB[0]));
    memcpy(entry.byPieceBB[side], pos.byPieceBB, sizeof(entry.byPieceBB[0]));
  }

  void Thread::updateAccumulator(Position& pos, NNUE::Accumulator& head) {

    for (Color side = WHITE; side <= BLACK; ++side) {
      if (head.updated[side])
        continue;

      const Square king = head.kings[side];
      NNUE::Accumulator* iter = &head;
      while (true) {
        iter--;

        if (NNUE::needRefresh(side, iter->kings[side], king)) {
          refreshAccumulator(pos, head, side);
          break;
        }

        if (iter->updated[side]) {
          NNUE::Accumulator* lastUpdated = iter;
          while (lastUpdated != &head) {
            (lastUpdated+1)->doUpdates(king, side, *lastUpdated);
            lastUpdated++;
          }
          break;
        }
      }
    }
  }

  Score Thread::doEvaluation(Position& pos) {
    NNUE::Accumulator& acc = accumStack[accumStackHead];
    updateAccumulator(pos, acc);
    return Eval::evaluate(pos, !(ply % 2), acc);
  }

  void Thread::playMove(Position& pos, Move move, SearchInfo* ss) {

    nodesSearched++;

    const bool isCap = pos.board[move_to(move)] != NO_PIECE;
    ss->contHistory = contHistory[isCap][pieceTo(pos, move)];
    ss->playedMove = move;
    ss->playedCap = ! pos.isQuiet(move);
    keyStack[keyStackHead++] = pos.key;

    NNUE::Accumulator& newAcc = accumStack[++accumStackHead];

    ply++;
    pos.doMove(move, newAcc.dirtyPieces);

    for (Color side = WHITE; side <= BLACK; ++side) {
      newAcc.updated[side] = false;
      newAcc.kings[side] = pos.kingSquare(side);
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

  Score Thread::adjustEval(Position &pos, Score eval) {
    // 50 move rule scaling
    eval = (eval * (200 - pos.halfMoveClock)) / 200;

    // Pawn correction history
    eval += PawnChWeight * pawnCorrhist[pos.sideToMove][getCorrHistIndex(pos.pawnKey)] / 512;
    eval += NonPawnChWeight * wNonPawnCorrhist[pos.sideToMove][getCorrHistIndex(pos.nonPawnKey[WHITE])] / 512;
    eval += NonPawnChWeight * bNonPawnCorrhist[pos.sideToMove][getCorrHistIndex(pos.nonPawnKey[BLACK])] / 512;

    return std::clamp(eval, SCORE_TB_LOSS_IN_MAX_PLY + 1, SCORE_TB_WIN_IN_MAX_PLY - 1);
  }

  void addToContHistory(Position& pos, int bonus, Move move, SearchInfo* ss) {
    int chIndex = pieceTo(pos, move);
    if ((ss - 1)->playedMove)
      addToHistory((ss - 1)->contHistory[chIndex], bonus);
    if ((ss - 2)->playedMove)
      addToHistory((ss - 2)->contHistory[chIndex], bonus);
    if ((ss - 4)->playedMove)
      addToHistory((ss - 4)->contHistory[chIndex], bonus);
    if ((ss - 6)->playedMove)
      addToHistory((ss - 6)->contHistory[chIndex], bonus);
  }

  void Thread::updateHistories(Position& pos, int bonus, int malus, Move bestMove,
                       Move* quiets, int quietCount, int depth, SearchInfo* ss) {

    // Counter move
    if ((ss - 1)->playedMove) {
      Square prevSq = move_to((ss - 1)->playedMove);
      counterMoveHistory[pos.board[prevSq] * SQUARE_NB + prevSq] = bestMove;
    }

    // Killer move
    ss->killerMove = bestMove;

    // Credits to Ethereal
    // Don't prop up the best move if it was a quick low depth cutoff
    if (depth <= 3 && !quietCount)
      return;

    // Butterfly history
    addToHistory(mainHistory[pos.sideToMove][move_from_to(bestMove)], bonus);

    // Continuation history
    addToContHistory(pos, bonus, bestMove, ss);

    // Decrease score of other quiet moves
    for (int i = 0; i < quietCount; i++) {
      Move otherMove = quiets[i];
      addToContHistory(pos, -malus, otherMove, ss);
      addToHistory(mainHistory[pos.sideToMove][move_from_to(otherMove)], -malus);
    }
  }

  bool canUseScore(TT::Flag bound, Score score, Score operand) {
    return bound & (score >= operand ? TT::FLAG_LOWER : TT::FLAG_UPPER);
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

  template<bool IsPV>
  Score Thread::qsearch(Position& pos, Score alpha, Score beta, int depth, SearchInfo* ss) {

    // Detect upcoming draw
    if (alpha < SCORE_DRAW && hasUpcomingRepetition(pos, ply)) {
      alpha = SCORE_DRAW;
      if (alpha >= beta)
        return alpha;
    }

    // Detect draw
    if (isRepetition(pos, ply) || pos.is50mrDraw())
      return SCORE_DRAW;

    // Quit if we are close to reaching max ply
    if (ply >= MAX_PLY-4)
      return pos.checkers ? SCORE_DRAW : adjustEval(pos, doEvaluation(pos));

    // Probe TT
    const Key posTtKey = pos.key ^ ZOBRIST_50MR[pos.halfMoveClock];
    bool ttHit;
    TT::Entry* ttEntry = TT::probe(posTtKey, ttHit);
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
    if ( !IsPV
      && ttScore != SCORE_NONE
      && canUseScore(ttBound, ttScore, beta))
        return ttScore;

    Move bestMove = MOVE_NONE;
    Score rawStaticEval;
    Score bestScore;
    Score futility;

    // Do the static evaluation

    if (pos.checkers) {
      // When in check avoid evaluating
      bestScore = -SCORE_INFINITE;
      futility = ss->staticEval = rawStaticEval = SCORE_NONE;
    }
    else {
      if (ttStaticEval != SCORE_NONE)
        rawStaticEval = ttStaticEval;
      else
        rawStaticEval = doEvaluation(pos);

      bestScore = ss->staticEval = adjustEval(pos, rawStaticEval);

      futility = bestScore + QsFpMargin;

      // When tt bound allows it, use ttScore as a better standing pat
      if (ttScore != SCORE_NONE && canUseScore(ttBound, ttScore, bestScore))
        bestScore = ttScore;

      if (bestScore >= beta) {
        if (! ttHit)
          ttEntry->store(posTtKey, TT::NO_FLAG, 0, MOVE_NONE, SCORE_NONE, rawStaticEval, false, ply);
        return (bestScore + beta) / 2;
      }
      if (bestScore > alpha)
        alpha = bestScore;
    }

    MovePicker movePicker(
      MovePicker::QSEARCH, pos,
      ttMove, MOVE_NONE, MOVE_NONE,
      mainHistory, captureHistory,
      0,
      ss);

    movePicker.genQuietChecks = (depth == 0);

    bool foundLegalMoves = false;

    // Visit moves
    Move move;

    while (move = movePicker.nextMove(false)) {

      TT::prefetch(pos.keyAfter(move));

      if (!pos.isLegal(move))
        continue;

      foundLegalMoves = true;

      bool isQuiet = pos.isQuiet(move);

      if (bestScore > SCORE_TB_LOSS_IN_MAX_PLY) {
        if (!isQuiet && !pos.checkers && futility <= alpha && !pos.seeGe(move, 1)) {
          bestScore = std::max(bestScore, futility);
          continue;
        }

        if (!pos.seeGe(move, QsSeeMargin))
          continue;
      }

      Position newPos = pos;
      playMove(newPos, move, ss);

      Score score = -qsearch<IsPV>(newPos, -beta, -alpha, depth - 1, ss + 1);

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
        if (pos.checkers && isQuiet)
          break;
      }
    }

    if (pos.checkers && !foundLegalMoves)
      return ply - SCORE_MATE;

    if (bestScore >= beta && abs(bestScore) < SCORE_TB_WIN_IN_MAX_PLY)
      bestScore = (bestScore + beta) / 2;

    ttEntry->store(posTtKey,
      bestScore >= beta ? TT::FLAG_LOWER : TT::FLAG_UPPER,
      0, bestMove, bestScore, rawStaticEval, ttPV, ply);

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
  Score Thread::negamax(Position& pos, Score alpha, Score beta, int depth, bool cutNode, SearchInfo* ss, const Move excludedMove) {

    const bool IsRoot = IsPV && ply == 0;

    // Check time
    ++maxTimeCounter;
    if ( this == Threads::mainThread()
      && (maxTimeCounter & 4095) == 0
      && elapsedTime() >= maxTime)
        Threads::stopSearch();

    if (Threads::isSearchStopped())
      return SCORE_DRAW;

    // Init node
    if (IsPV)
      ss->pvLength = ply;

    // Enter qsearch when depth is 0
    if (depth <= 0)
      return qsearch<IsPV>(pos, alpha, beta, 0, ss);

    // Detect upcoming draw
    if (!IsRoot && alpha < SCORE_DRAW && hasUpcomingRepetition(pos, ply)) {
      alpha = SCORE_DRAW;
      if (alpha >= beta)
        return alpha;
    }

    // Detect draw
    if (!IsRoot && (isRepetition(pos, ply) || pos.is50mrDraw()))
      return SCORE_DRAW;

    // Quit if we are close to reaching max ply
    if (ply >= MAX_PLY - 4)
      return pos.checkers ? SCORE_DRAW : adjustEval(pos, doEvaluation(pos));

    // Mate distance pruning
    alpha = std::max(alpha, ply - SCORE_MATE);
    beta = std::min(beta, SCORE_MATE - ply - 1);
    if (alpha >= beta)
      return alpha;

    // Probe TT
    const Key posTtKey = pos.key ^ ZOBRIST_50MR[pos.halfMoveClock];
    bool ttHit;
    TT::Entry* ttEntry = TT::probe(posTtKey, ttHit);

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
    Score rawStaticEval = SCORE_NONE;
    Score bestScore = -SCORE_INFINITE;
    Score maxScore  =  SCORE_INFINITE;

    // In non PV nodes, if tt depth and bound allow it, return ttScore
    if ( !IsPV
      && !excludedMove
      && ttScore != SCORE_NONE
      && ttDepth >= depth
      && canUseScore(ttBound, ttScore, beta)
      && pos.halfMoveClock < 90) // The TT entry might trick us into thinking this is not a draw
        return ttScore;

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
        ttEntry->store(posTtKey, tbBound, depth, MOVE_NONE, tbScore, SCORE_NONE, ttPV, ply);
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

    bool improving = false;

    // Do the static evaluation

    if (pos.checkers) {
      // When in check avoid evaluating and skip pruning
      ss->staticEval = eval = SCORE_NONE;
      goto moves_loop;
    }
    else if (excludedMove) {
      // We have already evaluated the position in the node which invoked this singular search
      updateAccumulator(pos, accumStack[accumStackHead]);
      rawStaticEval = eval = ss->staticEval;
    }
    else {
      if (ttStaticEval != SCORE_NONE) {
        rawStaticEval = ttStaticEval;
        if (IsPV)
          updateAccumulator(pos, accumStack[accumStackHead]);
      }
      else
        rawStaticEval = doEvaluation(pos);

      eval = ss->staticEval = adjustEval(pos, rawStaticEval);

      if (!ttHit) {
        // This (probably new) position has just been evaluated.
        // Immediately save the evaluation in TT, so other threads who reach this position
        // won't need to evaluate again
        // This is also helpful when we cutoff early and no other store will be performed
        ttEntry->store(posTtKey, TT::NO_FLAG, 0, MOVE_NONE, SCORE_NONE, rawStaticEval, ttPV, ply);
      }

      // When tt bound allows it, use ttScore as a better evaluation
      if (ttScore != SCORE_NONE && canUseScore(ttBound, ttScore, eval))
        eval = ttScore;
    }

    if (!(ss-1)->playedCap && (ss-1)->staticEval != SCORE_NONE) {
      int theirLoss = (ss-1)->staticEval + ss->staticEval - EvalHistA;
      int bonus = std::clamp(EvalHistB * theirLoss / 64, -EvalHistC, EvalHistC);
      addToHistory(mainHistory[~pos.sideToMove][move_from_to((ss-1)->playedMove)], bonus);
    }

    // Calculate whether the evaluation here is worse or better than it was 2 plies ago
    if ((ss - 2)->staticEval != SCORE_NONE)
      improving = ss->staticEval > (ss - 2)->staticEval;
    else if ((ss - 4)->staticEval != SCORE_NONE)
      improving = ss->staticEval > (ss - 4)->staticEval;

    // Razoring. When evaluation is far below alpha, we could probably only catch up with a capture,
    // thus do a qsearch. If the qsearch still can't hit alpha, cut off
    if ( !IsPV
      && alpha < 2000
      && eval < alpha - RazoringDepthMul * depth) {
      Score score = qsearch<IsPV>(pos, alpha, beta, 0, ss);
      if (score <= alpha)
        return score;
    }

    // Reverse futility pruning. When evaluation is far above beta, assume that at least a move
    // will return a similarly high score, so cut off
    if ( !IsPV
      && depth <= RfpMaxDepth
      && eval < SCORE_TB_WIN_IN_MAX_PLY
      && eval - std::max(RfpDepthMul * (depth - improving), 20) >= beta)
      return (eval + beta) / 2;

    // Null move pruning. When our evaluation is above beta, we give the opponent
    // a free move, and if we are still better, cut off
    if ( !IsPV
      && !excludedMove
      && (ss - 1)->playedMove != MOVE_NONE
      && eval >= beta
      && ss->staticEval + NmpA * depth - NmpB >= beta
      && pos.hasNonPawns(pos.sideToMove)
      && beta > SCORE_TB_LOSS_IN_MAX_PLY) {

      TT::prefetch(pos.key ^ ZOBRIST_TEMPO);

      int R = std::min((eval - beta) / NmpEvalDiv, (int)NmpEvalDivMin) + depth / NmpDepthDiv + NmpBase + ttMoveNoisy;

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
      bool visitTTMove = ttMoveNoisy && pos.seeGe(ttMove, pcSeeMargin);

      MovePicker pcMovePicker(
        MovePicker::PROBCUT, pos,
        visitTTMove ? ttMove : MOVE_NONE, MOVE_NONE, MOVE_NONE,
        mainHistory, captureHistory,
        pcSeeMargin,
        ss);

      Move move;

      while (move = pcMovePicker.nextMove(false)) {

        TT::prefetch(pos.keyAfter(move));

        if (!pos.isLegal(move))
          continue;

        Position newPos = pos;
        playMove(newPos, move, ss);

        Score score = -qsearch<false>(newPos, -probcutBeta, -probcutBeta + 1, 0, ss + 1);

        // Do a normal search if qsearch was positive
        if (score >= probcutBeta)
          score = -negamax<false>(newPos, -probcutBeta, -probcutBeta + 1, depth - 4, !cutNode, ss + 1);

        cancelMove();

        if (Threads::isSearchStopped())
          return SCORE_DRAW;

        if (score >= probcutBeta) {
          ttEntry->store(posTtKey, TT::FLAG_LOWER, depth - 3, move, score, rawStaticEval, ttPV, ply);
          return score;
        }
      }
    }

  moves_loop:

    // Generate moves and score them

    bool skipQuiets = false;
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
      0,
      ss);

    // Visit moves

    Move move;

    while (move = movePicker.nextMove(skipQuiets)) {
      if (move == excludedMove)
        continue;

      TT::prefetch(pos.keyAfter(move));

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
        int lmrRed = lmrTable[depth][seenMoves] + !improving - history / EarlyLmrHistoryDiv;
        int lmrDepth = std::max(0, depth - lmrRed);

        // SEE (Static Exchange Evalution) pruning
        int seeMargin = isQuiet ? PvsQuietSeeMargin * lmrDepth * lmrDepth :
                                  PvsCapSeeMargin * depth;
        if (!pos.seeGe(move, seeMargin))
          continue;

        if (isQuiet && history < HistPrDepthMul * depth)
            skipQuiets = true;

        // Late move pruning. At low depths, only visit a few quiet moves
        if (seenMoves >= (depth * depth + LmpBase) / (2 - improving))
          skipQuiets = true;

        // Futility pruning. If our evaluation is far below alpha,
        // only visit a few quiet moves
        if (   isQuiet
            && lmrDepth <= FpMaxDepth
            && !pos.checkers
            && ss->staticEval + FpBase + FpDepthMul * lmrDepth <= alpha) {
          skipQuiets = true;
          continue;
        }
      }

      int extension = 0;

      // Singular extension
      if ( !IsRoot
        && ply < 2 * rootDepth
        && depth >= 5
        && !excludedMove
        && move == ttMove
        && abs(ttScore) < SCORE_TB_WIN_IN_MAX_PLY
        && ttBound & TT::FLAG_LOWER
        && ttDepth >= depth - 3)
      {
        Score singularBeta = ttScore - (depth * SBetaMargin) / 64;

        Score seScore = negamax<false>(pos, singularBeta - 1, singularBeta, (depth - 1) / 2, cutNode, ss, move);

        if (seScore < singularBeta) {
          // Extend even more if s. value is smaller than s. beta by some margin
          if (   !IsPV
              && seScore < singularBeta - DoubleExtMargin)
          {
            extension = 2 + (isQuiet && seScore < singularBeta - TripleExtMargin);
          } else {
            extension = 1;
          }
        }
        else if (singularBeta >= beta) // Multicut
          return singularBeta;
        else if (ttScore >= beta) // Negative extensions
          extension = -2 + IsPV;
        else if (cutNode)
          extension = -2;
      }

      Position newPos = pos;
      playMove(newPos, move, ss);

      int newDepth = depth + extension - 1;

      Score score;

      // Late move reductions

      if (depth >= 2 && seenMoves > 1 + 2 * IsRoot) {

        int R = lmrTable[depth][seenMoves];

        R -= history / (isQuiet ? LmrQuietHistoryDiv : LmrCapHistoryDiv);

        R -= (newPos.checkers != 0ULL);

        R -= (ttDepth >= depth);

        R -= ttPV + IsPV;

        R += ttMoveNoisy;

        R += !improving;

        R += 2 * cutNode;

        // Clamp to avoid a qsearch or an extension in the child search
        int reducedDepth = std::clamp(newDepth - R, 1, newDepth + 1);

        score = -negamax<false>(newPos, -alpha - 1, -alpha, reducedDepth, true, ss + 1);

        if (score > alpha && reducedDepth < newDepth) {
          newDepth += (score > bestScore + ZwsDeeperMargin);
          newDepth -= (score < bestScore + newDepth        && !IsRoot);

          if (reducedDepth < newDepth)
            score = -negamax<false>(newPos, -alpha - 1, -alpha, newDepth, !cutNode, ss + 1);

          int bonus = score <= alpha ? -statMalus(newDepth) : score >= beta ? statBonus(newDepth) : 0;
          addToContHistory(pos, bonus, move, ss);
        }
      }
      else if (!IsPV || seenMoves > 1)
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

    // Only in pv nodes we could probe TB and not cut off immediately
    if (IsPV)
      bestScore = std::min(bestScore, maxScore);

    // Update histories
    if (bestScore >= beta)
    {
      int bonus = statBonus(depth + (bestScore > beta + StatBonusBoostAt));
      int malus = statMalus(depth + (bestScore > beta + StatBonusBoostAt));

      if (pos.isQuiet(bestMove))
      {
        updateHistories(pos, bonus, malus, bestMove, quiets, quietCount, depth, ss);
      }
      else {
        PieceType captured = piece_type(pos.board[move_to(bestMove)]);
        addToHistory(captureHistory[pieceTo(pos, bestMove)][captured], bonus);
      }

      for (int i = 0; i < captureCount; i++) {
        Move otherMove = captures[i];
        PieceType captured = piece_type(pos.board[move_to(otherMove)]);
        addToHistory(captureHistory[pieceTo(pos, otherMove)][captured], -malus);
      }
    }

    TT::Flag resultBound;
    if (bestScore >= beta)
      resultBound = TT::FLAG_LOWER;
    else
      resultBound = (IsPV && bestMove) ? TT::FLAG_EXACT : TT::FLAG_UPPER;

    // update corrhist
    const bool bestMoveCap = pos.board[move_to(bestMove)] != NO_PIECE;
    if (   !pos.checkers
        && !(bestMove && bestMoveCap)
        && canUseScore(resultBound, bestScore, ss->staticEval))
    {
      int bonus = std::clamp((bestScore - ss->staticEval) * depth / 8,
                             -CORRHIST_LIMIT / 4, CORRHIST_LIMIT / 4);
                  
      addToCorrhist(pawnCorrhist[pos.sideToMove][getCorrHistIndex(pos.pawnKey)], bonus);
      addToCorrhist(wNonPawnCorrhist[pos.sideToMove][getCorrHistIndex(pos.nonPawnKey[WHITE])], bonus);
      addToCorrhist(bNonPawnCorrhist[pos.sideToMove][getCorrHistIndex(pos.nonPawnKey[BLACK])], bonus);
    }

    // Store to TT
    if (!excludedMove && !(IsRoot && pvIdx > 0))
      ttEntry->store(posTtKey, resultBound, depth, bestMove, bestScore, rawStaticEval, ttPV, ply);

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

  DEFINE_PARAM_B(tm0, 192, 50, 200);
  DEFINE_PARAM_B(tm1, 61,  20, 100);

  DEFINE_PARAM_B(tm2, 169, 50, 200);
  DEFINE_PARAM_B(tm3, 7,   0, 30);

  DEFINE_PARAM_B(tm4, 84,   0, 150);
  DEFINE_PARAM_B(tm5, 10,   0, 20);
  DEFINE_PARAM_B(tm6, 26,   0, 50);

  DEFINE_PARAM_B(lol0, 81, 0, 150);
  DEFINE_PARAM_B(lol1, 146,   75,  225);

  void Thread::startSearch() {

    const Settings& settings = Threads::getSearchSettings();

    Position rootPos = settings.position;

    accumStackHead = 0;
    for (Color side = WHITE; side <= BLACK; ++side) {
      accumStack[0].refresh(rootPos, side);
      accumStack[0].kings[side] = rootPos.kingSquare(side);
    }

    for (int i = 0; i < 2; i++)
        for (int j = 0; j < NNUE::KingBuckets; j++)
          finny[i][j].reset();

    keyStackHead = 0;
    for (int i = 0; i < settings.prevPositions.size(); i++)
      keyStack[keyStackHead++] = settings.prevPositions[i];

    maxTime = 999999999999LL;

    if (settings.standardTimeLimit()) {
      int64_t stdMaxTime;
      TimeMan::calcOptimumTime(settings, rootPos.sideToMove, &optimumTime, &stdMaxTime);
      maxTime = std::min(maxTime, stdMaxTime);
    }
    if (settings.movetime)
      maxTime = std::min(maxTime, settings.movetime - int64_t(UCI::Options["Move Overhead"]));

    ply = 0;
    maxTimeCounter = 0;

    // Setup search stack

    SearchInfo* ss = &searchStack[SsOffset];

    for (int i = 0; i < MAX_PLY + SsOffset; i++) {
      searchStack[i].staticEval = SCORE_NONE;

      searchStack[i].pvLength = 0;

      searchStack[i].killerMove = MOVE_NONE;
      searchStack[i].playedMove = MOVE_NONE;
      searchStack[i].playedCap = false;

      searchStack[i].contHistory = contHistory[false][0];
    }

    bool naturalExit = true;

    // TM variables
    Move idPrevMove = MOVE_NONE;
    Score idPrevScore = SCORE_NONE;
    int searchStability = 0;

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

    Move tbBestMove = MOVE_NONE;

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

      if (result != TB_RESULT_FAILED)
        tbBestMove = moveFromTbProbeRoot(result);
    }

    // Search starting. Zero out the nodes of each root move
    for (int i = 0; i < rootMoves.size(); i++)
      rootMoves[i].nodes = 0;

    const int multiPV = std::min(int(UCI::Options["MultiPV"]), rootMoves.size());

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

          int adjustedDepth = std::max(1, rootDepth - failHighCount);

          Score score = negamax<true>(rootPos, alpha, beta, adjustedDepth, false, ss);

          // The score of any root move is updated only if search wasn't yet stopped at the moment of updating.
          // This means that the root moves' score is usable at any time
          sortRootMoves(pvIdx);

          if (Threads::isSearchStopped()) {
            naturalExit = false;
            goto bestMoveDecided;
          }

          if (score <= alpha) {
            beta = (alpha + beta) / 2;
            alpha = std::max(-SCORE_INFINITE, score - window);

            failHighCount = 0;
          }
          else if (score >= beta) {
            beta = std::min(SCORE_INFINITE, score + window);

            if (score < 2000)
              failHighCount++;
          }
          else
            break;

          if (settings.nodes && Threads::totalNodes() >= settings.nodes) {
            naturalExit = false;
            goto bestMoveDecided;
          }

          window += window / 3;
        }

        sortRootMoves(0);
      }

      completeDepth = rootDepth;

      if (settings.nodes && Threads::totalNodes() >= settings.nodes) {
        naturalExit = false;
        goto bestMoveDecided;
      }

      if (this != Threads::mainThread())
        continue;

      const int64_t elapsed = elapsedTime();

      if (std::string(UCI::Options["Minimal"]) != "true")
        for (int i = 0; i < multiPV; i++)
          printInfo(completeDepth, i+1, rootMoves[i].score, getPvString(rootMoves[i]));

      if (elapsedTime() >= maxTime)
        goto bestMoveDecided;

      const Move bestMove = rootMoves[0].move;
      const Score score = rootMoves[0].score;

      if (bestMove == idPrevMove)
        searchStability = std::min(searchStability + 1, 8);
      else
        searchStability = 0;

      if (settings.standardTimeLimit() && rootDepth >= 4) {
        int bmNodes = rootMoves[rootMoves.indexOf(bestMove)].nodes;
        double notBestNodes = 1.0 - (bmNodes / double(nodesSearched));
        double nodesFactor     = (tm1/100.0) + notBestNodes * (tm0/100.0);

        double stabilityFactor = (tm2/100.0) - searchStability * (tm3/100.0);

        double scoreLoss =   (tm4/100.0)
                           + (tm5/1000.0) * (idPrevScore     - score)
                           + (tm6/1000.0) * (searchPrevScore - score);

        double scoreFactor = std::clamp(scoreLoss, lol0 / 100.0, lol1 / 100.0);

        if (elapsed > stabilityFactor * nodesFactor * scoreFactor * optimumTime)
          goto bestMoveDecided;
      }

      idPrevMove = bestMove;
      idPrevScore = score;
    }

  bestMoveDecided:

    // NOTE: When implementing best thread selection, don't mess up with tablebases dtz stuff

    if (this != Threads::mainThread())
      return;

    Threads::stopSearch();

    Threads::waitForSearch(false);

    Search::Thread* bestThread = this;

    if (rootMoves.size() > 1 && Threads::searchThreads.size() > 1) {

      std::unordered_map<Move, int> votes;
      Score minScore = SCORE_INFINITE;

      for (int i = 0; i < Threads::searchThreads.size(); i++) {
        Search::Thread* st = Threads::searchThreads[i];
        if (! st->completeDepth)
          continue;
        minScore = std::min(minScore, st->rootMoves[0].score);
      }

      for (int i = 0; i < Threads::searchThreads.size(); i++) {
        Search::Thread* st = Threads::searchThreads[i];
        if (! st->completeDepth)
          continue;
        votes[st->rootMoves[0].move] += (st->rootMoves[0].score - minScore + 9) * st->completeDepth;
      }

      for (int i = 1; i < Threads::searchThreads.size(); i++) {
        Search::Thread* st = Threads::searchThreads[i];
        if (! st->completeDepth)
          continue;
        Score currScore = st->rootMoves[0].score;
        int currVote = votes[st->rootMoves[0].move];
        Score bestScore = bestThread->rootMoves[0].score;
        int bestVote = votes[bestThread->rootMoves[0].move];

        if (abs(bestScore) >= SCORE_TB_WIN_IN_MAX_PLY) {
          if (currScore > bestScore)
            bestThread = st;
        } else if (currScore >= SCORE_TB_WIN_IN_MAX_PLY)
          bestThread = st;
        else if ( currScore > SCORE_TB_LOSS_IN_MAX_PLY && currVote > bestVote)
          bestThread = st;
      }
    }

    if (!naturalExit || bestThread != this || std::string(UCI::Options["Minimal"]) == "true")
        for (int i = 0; i < multiPV; i++)
          printInfo(bestThread->completeDepth, i+1, bestThread->rootMoves[i].score, getPvString(bestThread->rootMoves[i]));

    searchPrevScore = bestThread->rootMoves[0].score;

    if (tbBestMove && std::abs(searchPrevScore) < SCORE_MATE_IN_MAX_PLY)
      printBestMove(tbBestMove);
    else
      printBestMove(bestThread->rootMoves[0].move);
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
