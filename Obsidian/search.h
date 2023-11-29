#pragma once

#include "history.h"
#include "nnue.h"
#include "position.h"
#include "types.h"

namespace Search {

  extern Move lastBestMove;
  extern clock_t lastSearchTimeSpan;
  extern bool doingBench;

  enum State {
    IDLE, RUNNING, STOP_PENDING
  };

  struct Settings {

    clock_t time[COLOR_NB], inc[COLOR_NB], movetime, startTime;
    int movestogo, depth;
    int64_t nodes;

    Position position;

    Settings() {
      time[WHITE] = time[BLACK] = inc[WHITE] = inc[BLACK] = movetime = 0;
      movestogo = depth = 0;
      nodes = 0;
    }

    inline bool hasTimeLimit() const {
      return time[WHITE] || time[BLACK];
    }
  };

  struct SearchLoopInfo {
    Score score;
    Move bestMove;
  };

  struct SearchInfo {
    Score staticEval;
    Move playedMove;

    Move killerMove;

    Move pv[MAX_PLY];
    int pvLength;

    int doubleExt;

    Move excludedMove;

    PieceToHistory* mContHistory;

    PieceToHistory& contHistory() {
      return *mContHistory;
    }
  };

  enum NodeType {
    PV, NonPV
  };

  class SearchThread {

  public:

    volatile Search::State searchState;
    volatile bool stopThread;
    std::thread thread;

    void resetHistories();
    uint64_t nodesSearched;

    SearchThread();
    
  private:
    
    Color rootColor;

    int rootDepth;

    int ply = 0;

    Key keyStack[MAX_PLY];
    NNUE::Accumulator accumulatorStack[MAX_PLY];

    MoveList rootMoves;

    MainHistory mainHistory;
    CaptureHistory captureHistory;
    ContinuationHistory contHistory;
    CounterMoveHistory counterMoveHistory;

    bool usedMostOfTime();

    void playNullMove(Position& pos, SearchInfo* ss);

    void playMove(Position& pos, Move move, SearchInfo* ss);

    void cancelMove();

    int getHistoryScore(Position& pos, Move move, SearchInfo* ss);

    void updateHistories(Position& pos, int depth, Move bestMove, Score bestScore,
      Score beta, Move* quietMoves, int quietCount, SearchInfo* ss);

    void scoreRootMoves(Position& pos, MoveList& moves, Move ttMove, SearchInfo* ss);

    // Should not be called from Root node
    bool is2FoldRepetition(Position& pos);

    Score makeDrawScore();

    template<NodeType nodeType>
    Score qsearch(Position& position, Score alpha, Score beta, SearchInfo* ss);

    template<NodeType nodeType>
    Score negaMax(Position& position, Score alpha, Score beta, int depth, bool cutNode, SearchInfo* ss);

    Score rootNegaMax(Position& position, Score alpha, Score beta, int depth, SearchInfo* ss);

    void startSearch();

    void idleLoop();
  };

  template<bool root>
  int64_t perft(Position& pos, int depth);

  void initLmrTable();

  void searchInit();
}