#include "zobrist.h"
#include <random>

uint64_t ZobristTempo;
uint64_t ZobristPsq[PIECE_NB][SQUARE_NB];
uint64_t ZobristEp[FILE_NB];
uint64_t ZobristCastling[16];

namespace Zobrist {

  void init() {
    std::mt19937_64 gen(12345);
    std::uniform_int_distribution<uint64_t> dis;

    ZobristTempo = dis(gen);

    for (int pc = W_PAWN; pc < PIECE_NB; ++pc)
      for (Square sq = SQ_A1; sq < SQUARE_NB; ++sq)
        ZobristPsq[pc][sq] = dis(gen);

    for (File f = FILE_A; f < FILE_NB; ++f)
      ZobristEp[f] = dis(gen);

    ZobristCastling[0] = 0;
    ZobristCastling[WHITE_OO] = dis(gen);
    ZobristCastling[WHITE_OOO] = dis(gen);
    ZobristCastling[BLACK_OO] = dis(gen);
    ZobristCastling[BLACK_OOO] = dis(gen);

    for (int i = 1; i < 16; i++) {
      
      if (BitCount(i) < 2)
        continue;

      uint64_t delta = 0;

      if (i & WHITE_OO)  delta ^= ZobristCastling[WHITE_OO];
      if (i & WHITE_OOO) delta ^= ZobristCastling[WHITE_OOO];
      if (i & BLACK_OO)  delta ^= ZobristCastling[BLACK_OO];
      if (i & BLACK_OOO) delta ^= ZobristCastling[BLACK_OOO];

      ZobristCastling[i] = delta;
    }
  }

}