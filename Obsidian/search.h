#pragma once

#include "history.h"
#include "nnue.h"
#include "position.h"
#include "types.h"

#include <condition_variable>
#include <vector>

namespace Search {

  struct Settings {

    clock_t time[COLOR_NB], inc[COLOR_NB], movetime, startTime;
    int movestogo, depth;
    uint64_t nodes;

    Position position;

    std::vector<uint64_t> prevPositions;

    Settings();

    inline bool standardTimeLimit() const {
      return time[WHITE] || time[BLACK];
    }
  };

  struct SearchInfo {
    Score staticEval;
    Move playedMove;

    Move killerMove;

    Move pv[MAX_PLY];
    int pvLength;

    int doubleExt;

    // [piece to]
    int* contHistory;
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
    
    clock_t optimumTime, maxTime;
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
    CaptureHistory captureHistory;
    ContinuationHistory contHistory;
    CounterMoveHistory counterMoveHistory;
    PawnCorrectionHistory pawnCorrhist;

    NNUE::FinnyTable finny;

    Score searchPrevScore;

    void refreshAccumulator(Position& pos, NNUE::Accumulator& acc, Color side);

    void sortRootMoves(int offset);

    bool visitRootMove(Move move);

    bool usedMostOfTime();

    void playNullMove(Position& pos, SearchInfo* ss);

    void cancelNullMove();

    void playMove(Position& pos, Move move, SearchInfo* ss);

    void cancelMove();

    int getQuietHistory(Position& pos, Move move, SearchInfo* ss);

    int getCapHistory(Position& pos, Move move);

    int correctStaticEval(Position &pos, Score staticEval);

    void updateHistories(Position& pos, int bonus, Move bestMove, Score bestScore,
      Score beta, Move* quietMoves, int quietCount, int depth, SearchInfo* ss);

    bool hasUpcomingRepetition(Position& pos, int ply);

    // Should not be called from Root node
    bool isRepetition(Position& pos, int ply);

    Score makeDrawScore();

    Score doEvaluation(Position& position);

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