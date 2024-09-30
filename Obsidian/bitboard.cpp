#include "bitboard.h"

#include <immintrin.h>
#include <iostream>


/*
* The 2 squares diagonal to the pawn, which he can capture
*/
Bitboard pawn_attacks[COLOR_NB][SQUARE_NB];


// attacks

Bitboard RookMasks[SQUARE_NB];
Bitboard BishopMasks[SQUARE_NB];

#if defined(USE_PEXT)

Bitboard* BishopAttacks[SQUARE_NB];
Bitboard* RookAttacks[SQUARE_NB];

Bitboard BishopTable[5248];
Bitboard RookTable[102400];

#else

Bitboard BishopAttacks[64][512];
Bitboard RookAttacks[64][4096];

#endif


Bitboard king_attacks[SQUARE_NB];

Bitboard knight_attacks[SQUARE_NB];


// other stuff

Bitboard BETWEEN_BB[SQUARE_NB][SQUARE_NB];
Bitboard LINE_BB[SQUARE_NB][SQUARE_NB];


// print bitboard
void printBitboard(Bitboard bitboard)
{
  // loop over board ranks
  for (Rank y = RANK_8; y >= RANK_1; --y)
  {
    // print rank
    std::cout << "  " << (y + 1) << " ";

    // loop over board files
    for (File x = FILE_A; x < FILE_NB; ++x)
    {
      // init board square
      Square square = makeSquare(x, y);

      // print bit indexed by board square
      std::cout << " " << (bitboard & square ? 1 : 0);
    }

    std::cout << std::endl;
  }

  // print files
  std::cout << "\n     a b c d e f g h" << std::endl;

  // print bitboard as decimal
  std::cout << "     bitboard: " << bitboard << std::endl;
}

// square encoding

int RookRelevantBits[64] = {
    12, 11, 11, 11, 11, 11, 11, 12,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    12, 11, 11, 11, 11, 11, 11, 12
};

int BishopRelevantBits[64] = {
    6, 5, 5, 5, 5, 5, 5, 6,
    5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 7, 7, 7, 7, 5, 5,
    5, 5, 7, 9, 9, 7, 5, 5,
    5, 5, 7, 9, 9, 7, 5, 5,
    5, 5, 7, 7, 7, 7, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5,
    6, 5, 5, 5, 5, 5, 5, 6
};

// set occupancies
Bitboard set_occupancy(int index, int bits_in_mask, Bitboard attack_mask)
{
    // occupancy map
    Bitboard occupancy = 0ULL;

    // loop over the range of bits within attack mask
    for (int count = 0; count < bits_in_mask; count++)
    {
        // get LS1B index of attacks mask
        Square square = popLsb(attack_mask);

        // make sure occupancy is on board
        if (index & (1 << count))
            // populate occupancy map
            occupancy |= square;
    }

    // return occupancy map
    return occupancy;
}

Bitboard gen_king_attacks(Square sqr) {
  Bitboard attacks = 0;
  File xLeft   = (File) std::max<int>(fileOf(sqr) - 1, 0);
  File xRight  = (File) std::min<int>(fileOf(sqr) + 1, 7);
  Rank yTop    = (Rank) std::max<int>(rankOf(sqr) - 1, 0);
  Rank yBottom = (Rank) std::min<int>(rankOf(sqr) + 1, 7);

  for (Rank y = yTop; y <= yBottom; ++y) {
    for (File x = xLeft; x <= xRight; ++x) {
      Square dest = makeSquare(x, y);
      if (sqr != dest)
        attacks |= dest;
    }
  }
  return attacks;
}

struct KnightMove {
  int offX, offY;
};

const KnightMove KnightMoves[] = {
    {2, 1}, {2, -1}, {-2, 1}, {-2, -1},
    {1, 2}, {1, -2}, {-1, 2}, {-1, -2} };

constexpr Direction RookDirs[] = { NORTH, EAST, SOUTH, WEST };
constexpr Direction BishopDirs[] = { NORTH_EAST, SOUTH_EAST, SOUTH_WEST, NORTH_WEST };

Bitboard gen_knight_attacks(Square sqr) {
  Bitboard attacks = 0;
  File x = fileOf(sqr);
  Rank y = rankOf(sqr);
  for (int i = 0; i < 8; i++) {
    File destX = x + KnightMoves[i].offX;
    Rank destY = y + KnightMoves[i].offY;
    if (destX >= 0 && destX < 8 && destY >= 0 && destY < 8) {
      attacks |= makeSquare(destX, destY);
    }
  }
  return attacks;
}

/*
*/
Bitboard gen_pawn_attacks(Color pawnColor, Square sqr) {
  if (pawnColor == WHITE && rankOf(sqr) == RANK_8)
    return 0;
  if (pawnColor == BLACK && rankOf(sqr) == RANK_1)
    return 0;

  Bitboard attacks = 0;

  if (fileOf(sqr) != FILE_A) { // then we can attack towards west
    attacks |= (pawnColor == WHITE ? (sqr + 7) : (sqr - 9));
  }
  if (fileOf(sqr) != FILE_H) { // then we can attack towards east
    attacks |= (pawnColor == WHITE ? (sqr + 9) : (sqr - 7));
  }
  return attacks;
}

int calcIncX(int direction) {
  switch (direction) {
  case EAST:
  case NORTH_EAST:
  case SOUTH_EAST: return 1;
  case WEST:
  case NORTH_WEST:
  case SOUTH_WEST: return -1;

  default: return 0;
  }
}

int calcIncY(int direction) {
  switch (direction) {
  case NORTH:
  case NORTH_EAST:
  case NORTH_WEST: return 1;
  case SOUTH:
  case SOUTH_EAST:
  case SOUTH_WEST: return -1;

  default: return 0;
  }
}

Bitboard sliding_attack(const Direction* dirs, Square s1, Bitboard occupied)
{
  Bitboard attack = 0;

  for (int i = 0; i < 4; i++) {

    File destX = fileOf(s1);
    Rank destY = rankOf(s1);

    int incX = calcIncX(dirs[i]);
    int incY = calcIncY(dirs[i]);

    while (true)
    {
      destX += incX;
      destY += incY;

      if (destX < 0 || destX > 7 || destY < 0 || destY > 7)
        break;

      attack |= makeSquare(destX, destY);

      if (occupied & makeSquare(destX, destY))
        break;
    }
  }

  return attack;
}

typedef uint32_t(AttackIndexFunc)(Square, Bitboard);

uint32_t attack_index_bishop(Square sq, Bitboard occupied) {
#if defined(USE_PEXT)
  return (uint32_t)_pext_u64(occupied, BishopMasks[sq]);
#else
  return (occupied & BishopMasks[sq]) * BISHOP_MAGICS[sq] >> 55;
#endif
}

uint32_t attack_index_rook(Square sq, Bitboard occupied) {
#if defined(USE_PEXT)
  return (uint32_t)_pext_u64(occupied, RookMasks[sq]);
#else
  return (occupied & RookMasks[sq]) * ROOK_MAGICS[sq] >> 52;
#endif
}

// lookup bishop attacks
Bitboard getBishopAttacks(Square sq, Bitboard occupied) {
	return BishopAttacks[sq][attack_index_bishop(sq, occupied)];
}

// lookup rook attacks
Bitboard getRookAttacks(Square sq, Bitboard occupied) {
	return RookAttacks[sq][attack_index_rook(sq, occupied)];
}

Bitboard getQueenAttacks(Square sq, Bitboard occupied) {
  return getBishopAttacks(sq, occupied) | getRookAttacks(sq, occupied);
}

Bitboard getBishopAttacks(Square sq) {
  return BishopAttacks[sq][0];
}

Bitboard getRookAttacks(Square sq) {
  return RookAttacks[sq][0];
}

Bitboard getKingAttacks(Square sq) {
  return king_attacks[sq];
}

Bitboard getKnightAttacks(Square sq) {
  return knight_attacks[sq];
}

Bitboard getPawnAttacks(Square sq, Color pawnColor) {
  return pawn_attacks[pawnColor][sq];
}

Bitboard getPawnBbAttacks(Bitboard pawns, Color pawnColor) {
  if (pawnColor == WHITE) {
    Bitboard east = (pawns & ~FILE_HBB) << 9;
    Bitboard west = (pawns & ~FILE_ABB) << 7;
    return east | west;
  } else {
    Bitboard east = (pawns & ~FILE_HBB) >> 7;
    Bitboard west = (pawns & ~FILE_ABB) >> 9;
    return east | west;
  }
}

Bitboard getPieceAttacks(PieceType pt, Square s, Bitboard occupied) {
  switch (pt) {
  case KNIGHT: return getKnightAttacks(s);
  case BISHOP: return getBishopAttacks(s, occupied);
  case ROOK:   return getRookAttacks(s, occupied);
  case QUEEN:  return getBishopAttacks(s, occupied) | getRookAttacks(s, occupied);
  case KING:   return getKingAttacks(s);
  default:     return 0;
  }
}

#if defined(USE_PEXT)

void init_pext_attacks(Bitboard table[], Bitboard* attacks[], Bitboard masks[],
  const Direction deltas[], AttackIndexFunc index)
{

  for (Square sq = SQ_A1; sq < SQUARE_NB; ++sq) {
    attacks[sq] = table;

    // Use Carry-Rippler trick to enumerate all subsets of masks[s] and
    // fill the attacks table.
    Bitboard b = 0;
    do {
      attacks[sq][index(sq, b)] = sliding_attack(deltas, sq, b);
      b = (b - masks[sq]) & masks[sq];
      table++;
    } while (b);
  }
}

#else

// init slider pieces attacks
void init_fancy_magic_attacks(Bitboard masks[],
  const Direction* deltas, PieceType pt)
{
    // loop over 64 board squares
    for (Square sq = SQ_A1; sq < SQUARE_NB; ++sq)
    {
        // count attack mask bits
        int bit_count = BitCount(masks[sq]);

        // occupancy variations count
        int occupancy_variations = 1 << bit_count;

        // loop over occupancy variations
        for (int count = 0; count < occupancy_variations; count++)
        {
            Bitboard occupancy = set_occupancy(count, bit_count, masks[sq]);

            if (pt == BISHOP) {
              Bitboard magic_index = occupancy * BISHOP_MAGICS[sq] >> 55;
              BishopAttacks[sq][magic_index] = sliding_attack(deltas, sq, occupancy);
            } else {
              Bitboard magic_index = occupancy * ROOK_MAGICS[sq] >> 52;
              RookAttacks[sq][magic_index] = sliding_attack(deltas, sq, occupancy);
            }
        }
    }
}

#endif

namespace Bitboards {

  void init() {
    for (Square sq = SQ_A1; sq < SQUARE_NB; ++sq) {
      king_attacks[sq] = gen_king_attacks(sq);

      knight_attacks[sq] = gen_knight_attacks(sq);

      pawn_attacks[WHITE][sq] = gen_pawn_attacks(WHITE, sq);
      pawn_attacks[BLACK][sq] = gen_pawn_attacks(BLACK, sq);

      // Board edges are not considered in the relevant occupancies
      Bitboard edges = ((Rank1BB | Rank8BB) & ~rankBB(sq)) | ((FILE_ABB | FILE_HBB) & ~fileBB(sq));

      BishopMasks[sq] = sliding_attack(BishopDirs, sq, 0) & ~edges;
      RookMasks[sq] = sliding_attack(RookDirs, sq, 0) & ~edges;
    }

    // Init sliding attacks

    #if defined(USE_PEXT)
    init_pext_attacks(RookTable, RookAttacks, RookMasks, RookDirs, attack_index_rook);
    init_pext_attacks(BishopTable, BishopAttacks, BishopMasks, BishopDirs, attack_index_bishop);
    #else
    init_fancy_magic_attacks(BishopMasks, BishopDirs, BISHOP); // bishop
    init_fancy_magic_attacks(RookMasks, RookDirs, ROOK); // rook
    #endif


    memset(LINE_BB, 0, sizeof(LINE_BB));
    memset(BETWEEN_BB, 0, sizeof(BETWEEN_BB));

    for (Square s1 = SQ_A1; s1 < SQUARE_NB; ++s1) {
      for (Square s2 = SQ_A1; s2 < SQUARE_NB; ++s2) {
        if (getBishopAttacks(s1) & s2) {
          BETWEEN_BB[s1][s2] = getBishopAttacks(s1, squareBB(s2)) & getBishopAttacks(s2, squareBB(s1));

          LINE_BB[s1][s2] = (getBishopAttacks(s1) & getBishopAttacks(s2)) | s1 | s2;
        }
        else  if (getRookAttacks(s1) & s2) {
          BETWEEN_BB[s1][s2] = getRookAttacks(s1, squareBB(s2)) & getRookAttacks(s2, squareBB(s1));

          LINE_BB[s1][s2] = (getRookAttacks(s1) & getRookAttacks(s2)) | s1 | s2;
        }
        BETWEEN_BB[s1][s2] |= s2;
      }
    }
  }

}
