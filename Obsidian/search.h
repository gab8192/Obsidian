#pragma once

#include "nnue.h"
#include "position.h"
#include "types.h"

namespace Search {

  extern Move lastBestMove;
  extern clock_t lastSearchTimeSpan;
  extern bool printingEnabled;
  extern uint64_t nodesSearched;

  enum State {
    IDLE, RUNNING, STOP_PENDING
  };

  struct Limits {

    clock_t time[COLOR_NB], inc[COLOR_NB], movetime, startTime;
    int movestogo, depth;
    int64_t nodes;

    Position position;

    Limits() {
      time[WHITE] = time[BLACK] = inc[WHITE] = inc[BLACK] = movetime = 0;
      movestogo = depth = 0;
      nodes = 0;
    }

    inline bool hasTimeLimit() const {
      return time[WHITE] || time[BLACK];
    }
  };

  template<bool root>
  int64_t perft(int depth);

  void initLmrTable();

  void searchInit();

  void clear();

  void* idleLoop(void*);
}