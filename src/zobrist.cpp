#include "zobrist.h"
#include <random>

uint64_t ZOBRIST_TEMPO;
uint64_t ZOBRIST_PSQ[PIECE_NB][SQUARE_NB];
uint64_t ZOBRIST_EP[FILE_NB];
uint64_t ZOBRIST_CASTLING[16];
uint64_t ZOBRIST_50MR[120];

namespace Zobrist {

  void init() {
    std::mt19937_64 gen(12345);
    std::uniform_int_distribution<uint64_t> dis;

    ZOBRIST_TEMPO = dis(gen);

    for (int pc = W_PAWN; pc < PIECE_NB; ++pc)
      for (Square sq = SQ_A1; sq < SQUARE_NB; ++sq)
        ZOBRIST_PSQ[pc][sq] = dis(gen);

    for (File f = FILE_A; f < FILE_NB; ++f)
      ZOBRIST_EP[f] = dis(gen);

    ZOBRIST_CASTLING[0] = 0;
    ZOBRIST_CASTLING[WHITE_OO] = dis(gen);
    ZOBRIST_CASTLING[WHITE_OOO] = dis(gen);
    ZOBRIST_CASTLING[BLACK_OO] = dis(gen);
    ZOBRIST_CASTLING[BLACK_OOO] = dis(gen);

    for (int i = 1; i < 16; i++) {

      if (BitCount(i) < 2)
        continue;

      uint64_t delta = 0;

      if (i & WHITE_OO)  delta ^= ZOBRIST_CASTLING[WHITE_OO];
      if (i & WHITE_OOO) delta ^= ZOBRIST_CASTLING[WHITE_OOO];
      if (i & BLACK_OO)  delta ^= ZOBRIST_CASTLING[BLACK_OO];
      if (i & BLACK_OOO) delta ^= ZOBRIST_CASTLING[BLACK_OOO];

      ZOBRIST_CASTLING[i] = delta;
    }

    // and now the 50mr stuff
    memset(ZOBRIST_50MR, 0, sizeof(ZOBRIST_50MR));
    for (int i = 14; i <= 100; i += 8) {
      uint64_t key = dis(gen);
      for (int j = 0; j < 8; j++)
        ZOBRIST_50MR[i+j] = key;
    }
  }

}
