#include "search.h"
#include "evaluate.h"
#include "movegen.h"
#include "timeman.h"
#include "threads.h"
#include "tt.h"
#include "uci.h"

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

	Move pv[MAX_PLY];
	int pvLength;
  };

  uint64_t nodesSearched;

  int selDepth;

  int rootDepth;

  int ply = 0;

  NNUE::Accumulator accumulatorStack[MAX_PLY];

  Position posStack[MAX_PLY];

  Position position;
  MoveList rootMoves;

  // assume 128 is the maximum moves
  int lmrTable[MAX_PLY][128];

  void clear() {
	TT::clear();
  }

  // Called one at engine initialization
  void searchInit() {

	for (int i = 0; i < MAX_PLY; i++) {
	  for (int m = 0; m < 128; m++) {
		lmrTable[i][m] = 0.75 + log(i) * log(m) / 2.25;
	  }
	}
  }

  NNUE::Accumulator* currentAccumulator() {
	return & accumulatorStack[ply];
  }
  
  inline void pushPosition() {
	memcpy(& posStack[ply], &position, sizeof(Position));

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
	  position.doMove(moves[i], & accumulatorStack[0]);

	  int thisNodes = perft<false>(depth - 1);
	  if constexpr (root) {
		cout << UCI::move(moves[i]) << " -> " << thisNodes << endl;
	  }

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

  inline void playNullMove(SearchInfo* ss) {
	nodesSearched++;
	ss->playedMove = MOVE_NONE;
	pushPosition();
	position.doNullMove();
  }

  inline void playMove(Move move, SearchInfo* ss) {
	nodesSearched++;
	ss->playedMove = move;
	pushPosition();
	position.doMove(move, & accumulatorStack[ply]);
  }

  inline void cancelMove() {
	popPosition();
  }

  void scoreMoves(MoveList& moves, Move ttMove) {
	for (int i = 0; i < moves.size(); i++) {
	  int& moveScore = moves.scores[i];

	  Move m = moves[i];
	  if (m == ttMove) {
		moveScore = 2000;
		continue;
	  }

	  // init it to 0
	  moveScore = 0;

	  switch (getMoveType(m)) {
	  case MT_NORMAL: {
		const Square from = getMoveSrc(m);
		const Square to = getMoveDest(m);
		const Piece movedPc = position.board[from];
		const Piece capturedPc = position.board[to];

		// mvv
		if (capturedPc != NO_PIECE)
		  moveScore += PieceValue[capturedPc];

		break;
	  }
	  case MT_CASTLING: {
		moveScore = 60;
		break;
	  }
	  case MT_EN_PASSANT: {
		moveScore = 80;
		break;
	  }
	  case MT_PROMOTION: {
		moveScore = PieceValue[getPromoType(m)];
		break;
	  }
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
	return  failsHigh ? TT::FLAG_BETA : TT::FLAG_ALPHA;
  }

  // Should not be called from Root node
  bool is2FoldRepetition() {
	int spN = seenPositions.size();
	for (int i = 0; i < spN; i++) {
	  if (position.key == seenPositions[i])
		return true;
	}

	// Start from ply=1  because  posStack[0] = seenPositions[last]
	for (int i = 1; i < ply; ++i) {
	  if (position.key == posStack[i].key)
		return true;
	}

	return false;
  }

  inline Value makeDrawValue() {
	return VALUE_DRAW + int(nodesSearched & 0x2);
  }

  template<NodeType nodeType>
  Value qsearch(Value alpha, Value beta, SearchInfo* ss) {
	constexpr bool PvNode = nodeType != NonPV;

	const Color us = position.sideToMove, them = ~us;

	TT::Entry* ttEntry = TT::probe(position);
	TT::Flag ttFlag = ttEntry->getFlag();
	Value ttValue = ttEntry->getValue();

	if (position.rule50_count >= 50)
	  return makeDrawValue();

	if (!PvNode 
	  && ttValue != VALUE_NONE) {

		if (ttFlag == TT::FLAG_EXACT || ttFlag == flagForTT(ttValue >= beta))
		  return ttValue;
	}

	Move bestMove = MOVE_NONE;
	Value bestValue;
	const Value oldAlpha = alpha;

	if (position.checkers) {
	  bestValue = -VALUE_INFINITE;
	}
	else {

	  if (ttEntry->getStaticEval() == VALUE_NONE)
		ttEntry->storeStaticEval(Eval::evaluate());

	  bestValue = ttEntry->getStaticEval();

	  if (ttValue != VALUE_NONE) {
		if (ttFlag == TT::FLAG_EXACT || ttFlag == flagForTT(ttValue > bestValue)) {
		  bestValue = ttValue;
		}
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

	scoreMoves(moves, ttEntry->getMove());

	bool foundLegalMoves = false; 

	for (int i = 0; i < moves.size(); i++) {
	  Move move = nextBestMove(moves, i);
	  if (!position.isLegal(move))
		continue;

	  foundLegalMoves = true;

	  if (getMoveType(move) == MT_NORMAL && !generateAllMoves) {
		if (!position.see_ge(move, Value(-95)))
		  continue;
	  }

	  playMove(move, ss);

	  Value value = -qsearch<nodeType>(-beta, -alpha, ss+1);

	  cancelMove();

	  if (value > bestValue) {
		bestValue = value;

		if (bestValue > alpha) {
		  bestMove = move;

		  // value >= beta is always true if beta==alpha+1 and value>alpha
		  if (!PvNode || bestValue >= beta) {
			ttEntry->store(TT::FLAG_BETA, 0, bestMove, bestValue);
			return bestValue;
		  }

		  // This is never reached on a NonPV node
		  alpha = bestValue;
		}
	  }
	}

	if (position.checkers && !foundLegalMoves)
	  return Value(ply - VALUE_MATE);

	ttEntry->store(alpha > oldAlpha ? TT::FLAG_EXACT : TT::FLAG_ALPHA, 0, bestMove, bestValue);

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
  Value negaMax(Value alpha, const Value beta, int depth, bool cutNode, SearchInfo* ss) {
	constexpr bool PvNode = nodeType != NonPV;
	constexpr bool rootNode = nodeType == Root;

	const Color us = position.sideToMove, them = ~us;

	if (PvNode) {
	  // init node
	  ss->pvLength = ply;

	  if (ply > selDepth)
		selDepth = ply;
	}

	if (!rootNode) {
	  if (is2FoldRepetition() || position.rule50_count >= 50)
		return makeDrawValue();
	}

	TT::Entry* ttEntry = TT::probe(position);
	TT::Flag ttFlag = ttEntry->getFlag();
	Value ttValue = ttEntry->getValue();
	Move ttMove = ttEntry->getMove();

	if (rootNode) {
	  if (!ttMove)
		ttMove = rootMoves[0];
	}

	Value eval;
	Move bestMove = MOVE_NONE;
	Value bestValue = -VALUE_INFINITE;
	const Value oldAlpha = alpha;

	if (position.checkers)
	  depth = myMax(1, depth+1);

	if (!PvNode 
	  && ttEntry->getDepth() >= depth
	  && ttValue != VALUE_NONE) {

		if (ttFlag == TT::FLAG_EXACT || ttFlag == flagForTT(ttValue >= beta))
		  return ttValue;
	}

    if (depth <= 0)
	  return qsearch<PvNode ? PV : NonPV>(alpha, beta, ss+1);

	bool improving = false;

	// Static evaluation of the position
	if (position.checkers) {
	  ss->staticEval = eval = VALUE_NONE;

	  // skip pruning when in check
	  goto moves_loop;
	}
	else {
	  if (ttEntry->getStaticEval() == VALUE_NONE)
		ttEntry->storeStaticEval(Eval::evaluate());

	  ss->staticEval = eval = ttEntry->getStaticEval();

	  if (ttValue != VALUE_NONE) {
		if (ttFlag == TT::FLAG_EXACT || ttFlag == flagForTT(ttValue > eval))
		  ss->staticEval = eval = ttValue;
	  }

	  if ((ss - 2)->staticEval != VALUE_NONE)
		improving = eval > (ss - 2)->staticEval;
	}

	// depth should always be >= 1 at this point

	// Razoring
	if (eval < alpha - 400 - 500 * depth) {
	  return qsearch<nodeType>(alpha, beta, ss+1);
	}

	// Null move pruning
	if (!PvNode
	  && (ss-1)->playedMove != MOVE_NONE
	  && eval >= beta
	  && position.hasNonPawns(position.sideToMove)
	  && beta > VALUE_TB_LOSS_IN_MAX_PLY) {

	  int R = myMin(int(eval - beta) / 200, 3) + depth / 3 + 4;

	  playNullMove(ss);
	  Value nullValue = -negaMax<NonPV>(-beta, -beta + 1, depth - R, !cutNode, ss+1);
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
	if (rootNode)
	  moves = rootMoves;
	else
	  getPseudoLegalMoves(position, &moves);
	scoreMoves(moves, ttMove);

	bool foundLegalMove = false;
	int playedMoves = 0;

	for (int i = 0; i < moves.size(); i++) {
	  Move move = nextBestMove(moves, i);

	  if (!position.isLegal(move))
		continue;

	  foundLegalMove = true;

	  // Pruning at shallow depth
	  if (nodeType != Root) {
		bool capture = false;
		if (getMoveType(move) == MT_NORMAL) {
		  capture = position.board[getMoveDest(move)] != NO_PIECE;
		}

		if (capture) {
		  if (!position.see_ge(move, Value(-260 * depth) ))
			continue;
		}
	  }
	  
	  playMove(move, ss);

	  Value value;

	  bool needFullSearch;
	  if (!wasInCheck && depth >= 3 && playedMoves > (1 + 2 * PvNode)) {
		int R = lmrTable[depth][myMin(128, playedMoves + 1)];

		R += !improving;
		R += !PvNode;

		// Do the clamp to avoid a qsearch or an extension in the child search
		int reducedDepth = myClamp(depth - R, 1, depth + 1);

		value = -negaMax<NonPV>(-alpha - 1, -alpha, reducedDepth, true, ss + 1);

		needFullSearch = value > alpha && reducedDepth < depth;
	  }
	  else
		needFullSearch = !PvNode || playedMoves >= 1;


	  if (needFullSearch)
		  value = -negaMax<NonPV>(-alpha - 1, -alpha, depth - 1, !cutNode, ss + 1);

	  if (PvNode && (playedMoves == 0 || value > alpha))
		value = -negaMax<PV>(-beta, -alpha, depth - 1, false, ss + 1);

	  cancelMove();

	  playedMoves++;

	  if (value > bestValue) {
		bestValue = value;

		if (bestValue > alpha) {
		  bestMove = move;

		  // value >= beta is always true if beta==alpha+1 and value>alpha
		  if (!PvNode || bestValue >= beta) {
			ttEntry->store(TT::FLAG_BETA, depth, bestMove, bestValue);
			return bestValue;
		  }

		  // This is never reached on a NonPV node

		  alpha = bestValue;

		  updatePV(ss, bestMove);
		}
	  }
	}

	if (!foundLegalMove)
	  return position.checkers ? Value(ply - VALUE_MATE) : VALUE_DRAW;

	ttEntry->store(alpha > oldAlpha ? TT::FLAG_EXACT : TT::FLAG_ALPHA, depth, bestMove, bestValue);

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

  constexpr int SsOffset = 2;

  SearchInfo searchStack[MAX_PLY + SsOffset];

  clock_t elapsedTime() {
	return clock() - searchLimits.startTime;
  }

  void startSearch() {

	clock_t optimumTime;

	if (searchLimits.hasTimeLimit())
	  optimumTime = TimeMan::calcOptimumTime(searchLimits, position.sideToMove);

	ply = 0;

	SearchLoopInfo iterDeepening[MAX_PLY];
	
	for (int i = 0; i < MAX_PLY + SsOffset; i++) {
	  searchStack[i].staticEval = VALUE_NONE;

	  searchStack[i].pvLength = 0;
	}

	SearchInfo* ss = &searchStack[SsOffset];

	if (searchLimits.depth == 0)
	  searchLimits.depth = MAX_PLY;

	nodesSearched = 0;

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
	scoreMoves(rootMoves, MOVE_NONE);

	for (rootDepth = 1; rootDepth <= searchLimits.depth; rootDepth++) {
	  selDepth = 0;

	  Value score;
	  if (rootDepth >= 4) {
		int windowSize = 10;
		Value alpha = iterDeepening[rootDepth - 1].score - windowSize;
		Value beta  = iterDeepening[rootDepth - 1].score + windowSize;

		int failedHighCnt = 0;
		while (true) {
		  
		  int adjustedDepth = myMax(1, rootDepth - failedHighCnt);

		  score = negaMax<Root>(alpha, beta, rootDepth, false, ss);

		  if (Threads::searchState == STOP_PENDING)
			goto bestMoveDecided;

		  if (searchLimits.hasTimeLimit()) {
			if (elapsedTime() >= optimumTime)
			  goto bestMoveDecided;
		  }

		  if (score <= alpha) {
			beta = Value((alpha + beta) / 2);
			alpha = (Value)myMax(-VALUE_INFINITE, alpha - windowSize);

			failedHighCnt = 0;
		  }
		  else if (score >= beta) {
			beta = (Value)myMin(VALUE_INFINITE, beta + windowSize);
			++failedHighCnt;
		  }
		  else
			break;

		  windowSize += windowSize/3;
		}
	  }
	  else {
		score = negaMax<Root>(-VALUE_INFINITE, VALUE_INFINITE, rootDepth, false, ss);
	  }

	  iterDeepening[rootDepth].selDepth = selDepth;
	  iterDeepening[rootDepth].score = score;
	  iterDeepening[rootDepth].bestMove = ss->pv[0];

	  clock_t elapsed = elapsedTime();

	  std::cout 
		<< "info" 
		<< " depth " << rootDepth
		<< " seldepth " << selDepth
		<< " score " << UCI::value(score)
		<< " nodes " << nodesSearched
		<< " nps " << (nodesSearched * 1000ULL) / myMax(elapsed, 1)
		<< " time " << elapsed
		<< " pv " << getPvString(ss)
		<< endl;

	  // Stop searching if we can deliver a forced checkmate.
	  // No need to stop if we are getting checkmated, instead keep searching,
	  // because we may have overlooked a way out of checkmate due to pruning
	  if (score >= VALUE_MATE_IN_MAX_PLY)
		goto bestMoveDecided;

	  // If the position is a dead draw, stop searching
	  if (rootDepth >= 8 && abs(score) < 4) {
		if (iterDeepening[rootDepth].selDepth == iterDeepening[rootDepth - 1].selDepth &&
		  iterDeepening[rootDepth].selDepth == iterDeepening[rootDepth - 2].selDepth)
		  goto bestMoveDecided;
	  }

	  if (searchLimits.hasTimeLimit() && rootDepth >= 4) {

		if (rootDepth > 12 && score == VALUE_DRAW) // FIXME
		  goto bestMoveDecided;

		if (elapsed > optimumTime)
		  goto bestMoveDecided;

		if (elapsed > optimumTime * 0.3) {

		  // And the best move is the same as that of prev iteration
		  bool sameBestMove = iterDeepening[rootDepth - 1].bestMove == iterDeepening[rootDepth].bestMove;
		  int scoreDiff = abs(iterDeepening[rootDepth - 1].score - iterDeepening[rootDepth].score);

		  // If the score is almost the same or the best move is stable, we can stop searching

		  if (scoreDiff < 10)
			goto bestMoveDecided;

		  if (sameBestMove && scoreDiff < 35)
			goto bestMoveDecided;
		  
		}
	  }
	}

  bestMoveDecided:

	std::cout << "bestmove " << UCI::move(ss->pv[0]) << endl;

	Threads::searchState = STOPPED;
  }

  unsigned idleLoop(void*) {
	while (true) {

	  while (Threads::searchState != RUNNING) {
		_mm_pause();
		continue;
	  }

	  startSearch();
	}
  }
}