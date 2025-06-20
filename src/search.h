#pragma once

#include "history.h"
#include "nnue.h"
#include "position.h"
#include "types.h"

#include <condition_variable>
#include <vector>

namespace Search {

  struct Settings {

    int64_t time[COLOR_NB], inc[COLOR_NB], movetime, startTime;
    int movestogo, depth;
    uint64_t nodes;

    Position position;

    std::vector<uint64_t> prevPositions;

    Settings();
  };

  struct SearchInfo {
    Score staticEval;
    int complexity;
    Move playedMove;
    bool playedCap;

    Move killerMove;

    Move pv[MAX_PLY];
    int pvLength;

    // [piece to]
    int16_t* contHistory;

    int seenMoves;
	
    // [piece to]
    int16_t* contCorrHist;
  };

  // A sort of header of the search stack, so that plies behind 0 are accessible and
  // it's easier to determine conthist score, improving, ...
  constexpr int SsOffset = 6;

  class Thread {

  public:

    std::mutex mutex;
    std::condition_variable cv;

    volatile bool searching = false;
    volatile bool exitThread = false;

    volatile int completeDepth;
    volatile uint64_t nodesSearched;
    volatile uint64_t tbHits;

    Thread();

    void resetHistories();

    void idleLoop();

  private:

    int64_t optimumTime, maxTime;
    uint32_t maxTimeCounter;

    int rootDepth;

    int ply = 0;

    int keyStackHead;
    Key keyStack[100 + MAX_PLY];

    int accumStackHead;
    NNUE::Accumulator accumStack[MAX_PLY];

    SearchInfo searchStack[MAX_PLY + SsOffset];

    RootMoveList rootMoves;
    int pvIdx;

    MainHistory mainHistory;
    PawnHistory pawnHistory;
    CaptureHistory captureHistory;
    ContinuationHistory contHistory;
    CounterMoveHistory counterMoveHistory;
    PawnCorrHist pawnCorrhist;
    NonPawnCorrHist wNonPawnCorrhist;
    NonPawnCorrHist bNonPawnCorrhist;
    ContCorrHist contCorrHist;

    NNUE::FinnyTable finny;

    Score searchPrevScore;

    void refreshAccumulator(Position& pos, NNUE::Accumulator& acc, Color side);

    void updateAccumulator(Position& pos, NNUE::Accumulator& acc);

    Score doEvaluation(Position& position);

    void sortRootMoves(int offset);

    bool visitRootMove(Move move);

    void playNullMove(Position& pos, SearchInfo* ss);

    void cancelNullMove();

    void playMove(Position& pos, Move move, SearchInfo* ss);

    void cancelMove();

    int getQuietHistory(Position& pos, Move move, SearchInfo* ss);

    int getCapHistory(Position& pos, Move move);

    // Perform adjustments such as 50MR, correction history, contempt, ...
    int adjustEval(Position &pos, Score staticEval, SearchInfo* ss);

    void updateMoveHistories(Position& pos, Move move, int bonus, SearchInfo* ss);

    bool hasUpcomingRepetition(Position& pos, int ply);

    // Should not be called from Root node
    bool isRepetition(Position& pos, int ply);

    template<bool IsPV>
    Score qsearch(Position& position, Score alpha, Score beta, int depth, SearchInfo* ss);

    template<bool IsPV>
    Score negamax(Position& position, Score alpha, Score beta, int depth,
      bool cutNode, SearchInfo* ss, const Move excludedMove = MOVE_NONE);

    void startSearch();
  };

  template<bool root>
  int64_t perft(Position& pos, int depth);

  void initLmrTable();

  void init();

  void printInfo(int depth, int pvIdx, Score score, const std::string& pvString);
}
