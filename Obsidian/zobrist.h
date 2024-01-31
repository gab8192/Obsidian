#pragma once

#include "types.h"

extern uint64_t ZobristTempo;
extern uint64_t ZobristPsq[PIECE_NB][SQUARE_NB];
extern uint64_t ZobristEp[FILE_NB];
extern uint64_t ZobristCastling[16];

namespace Zobrist {

  void init();

}