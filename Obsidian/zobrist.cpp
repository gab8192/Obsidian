#include "zobrist.h"
#include <random>

uint64_t ZobristTempo;
uint64_t ZobristPsq[PIECE_NB][SQUARE_NB];
uint64_t ZobristEp[FILE_NB];
uint64_t ZobristCastling[16];

class PRNG {

    uint64_t s;

    uint64_t rand64() {

        s ^= s >> 12, s ^= s << 25, s ^= s >> 27;
        return s * 2685821657736338717LL;
    }

   public:
    PRNG(uint64_t seed) :
        s(seed) {
    }

    template<typename T>
    T rand() {
        return T(rand64());
    }

    // Special generator used to fast init magic numbers.
    // Output values only have 1/8th of their bits set on average.
    template<typename T>
    T sparse_rand() {
        return T(rand64() & rand64() & rand64());
    }
};

void zobristInit() {
  
  PRNG rng(1070372);

  ZobristTempo = rng.rand<Key>();

  for (int pc = W_PAWN; pc < PIECE_NB; ++pc)
    for (Square sq = SQ_A1; sq < SQUARE_NB; ++sq)
      ZobristPsq[pc][sq] = rng.rand<Key>();

  for (File f = FILE_A; f < FILE_NB; ++f)
    ZobristEp[f] = rng.rand<Key>();

  ZobristCastling[0] = 0;
  ZobristCastling[WHITE_OO] = rng.rand<Key>();
  ZobristCastling[WHITE_OOO] = rng.rand<Key>();
  ZobristCastling[BLACK_OO] = rng.rand<Key>();
  ZobristCastling[BLACK_OOO] = rng.rand<Key>();

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