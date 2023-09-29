#pragma once

#include "nnue.h"
#include "position.h"
#include "types.h"

namespace Search {

  extern Position position;
  extern NNUE::Accumulator accumulatorStack[MAX_PLY];

  extern uint64_t nodesSearched;

  enum State {
    STOPPED, RUNNING, STOP_PENDING
  };

  struct Limits {

    clock_t time[COLOR_NB], inc[COLOR_NB], movetime, startTime;
    int movestogo, depth;
    int64_t nodes;

    Limits() {
      time[WHITE] = time[BLACK] = inc[WHITE] = inc[BLACK] = movetime = 0;
      movestogo = depth = 0;
      nodes = 0;
    }

    inline bool hasTimeLimit() const {
      return time[WHITE] || time[BLACK];
    }
  };

  NNUE::Accumulator* currentAccumulator();

  template<bool root>
  int perft(int depth);

  void searchInit();

  void clear();

  void startSearch();

  void* idleLoop(void*);
}