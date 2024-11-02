#pragma once

#include "types.h"

extern uint64_t ZOBRIST_TEMPO;
extern uint64_t ZOBRIST_PSQ[PIECE_NB][SQUARE_NB];
extern uint64_t ZOBRIST_EP[FILE_NB];
extern uint64_t ZOBRIST_CASTLING[16];
extern uint64_t ZOBRIST_50MR[120];

namespace Zobrist {

  void init();

}