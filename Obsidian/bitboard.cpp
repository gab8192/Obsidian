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

Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
Bitboard LineBB[SQUARE_NB][SQUARE_NB];


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
      Square square = make_square(x, y);

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
  File xLeft   = (File) std::max<int>(file_of(sqr) - 1, 0);
  File xRight  = (File) std::min<int>(file_of(sqr) + 1, 7);
  Rank yTop    = (Rank) std::max<int>(rank_of(sqr) - 1, 0);
  Rank yBottom = (Rank) std::min<int>(rank_of(sqr) + 1, 7);

  for (Rank y = yTop; y <= yBottom; ++y) {
    for (File x = xLeft; x <= xRight; ++x) {
      Square dest = make_square(x, y);
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
  File x = file_of(sqr);
  Rank y = rank_of(sqr);
  for (int i = 0; i < 8; i++) {
    File destX = x + KnightMoves[i].offX;
    Rank destY = y + KnightMoves[i].offY;
    if (destX >= 0 && destX < 8 && destY >= 0 && destY < 8) {
      attacks |= make_square(destX, destY);
    }
  }
  return attacks;
}

/*
*/
Bitboard gen_pawn_attacks(Color pawnColor, Square sqr) {
  if (pawnColor == WHITE && rank_of(sqr) == RANK_8)
    return 0;
  if (pawnColor == BLACK && rank_of(sqr) == RANK_1)
    return 0;

  Bitboard attacks = 0;

  if (file_of(sqr) != FILE_A) { // then we can attack towards west
    attacks |= (pawnColor == WHITE ? (sqr + 7) : (sqr - 9));
  }
  if (file_of(sqr) != FILE_H) { // then we can attack towards east
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

    File destX = file_of(s1);
    Rank destY = rank_of(s1);

    int incX = calcIncX(dirs[i]);
    int incY = calcIncY(dirs[i]);

    while (true)
    {
      destX += incX;
      destY += incY;

      if (destX < 0 || destX > 7 || destY < 0 || destY > 7)
        break;

      attack |= make_square(destX, destY);

      if (occupied & make_square(destX, destY))
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
  occupied &= BishopMasks[sq];
  occupied *= BishopMagics[sq];
  return occupied >> (64 - BishopRelevantBits[sq]);
#endif
}

uint32_t attack_index_rook(Square sq, Bitboard occupied) {
#if defined(USE_PEXT)
  return (uint32_t)_pext_u64(occupied, RookMasks[sq]);
#else
  occupied &= RookMasks[sq];
  occupied *= RookMagics[sq];
  return occupied >> (64 - RookRelevantBits[sq]);
#endif
}

// lookup bishop attacks 
Bitboard get_bishop_attacks(Square sq, Bitboard occupied) {
	return BishopAttacks[sq][attack_index_bishop(sq, occupied)];
}

// lookup rook attacks 
Bitboard get_rook_attacks(Square sq, Bitboard occupied) {  
	return RookAttacks[sq][attack_index_rook(sq, occupied)];
}

Bitboard get_bishop_attacks(Square sq) {
  return BishopAttacks[sq][0];
}

Bitboard get_rook_attacks(Square sq) {
  return RookAttacks[sq][0];
}

Bitboard get_king_attacks(Square sq) {
  return king_attacks[sq];
}

Bitboard get_knight_attacks(Square sq) {
  return knight_attacks[sq];
}

Bitboard get_pawn_attacks(Square sq, Color pawnColor) {
  return pawn_attacks[pawnColor][sq];
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
              Bitboard magic_index = occupancy * BishopMagics[sq] >> 64 - BishopRelevantBits[sq]; 
              BishopAttacks[sq][magic_index] = sliding_attack(deltas, sq, occupancy); 
            } else {
              Bitboard magic_index = occupancy * RookMagics[sq] >> 64 - RookRelevantBits[sq]; 
              RookAttacks[sq][magic_index] = sliding_attack(deltas, sq, occupancy); 
            }
        }
    }
}

#endif

void bitboardsInit() {
  for (Square sq = SQ_A1; sq < SQUARE_NB; ++sq) {
    king_attacks[sq] = gen_king_attacks(sq);

    knight_attacks[sq] = gen_knight_attacks(sq);

    pawn_attacks[WHITE][sq] = gen_pawn_attacks(WHITE, sq);
    pawn_attacks[BLACK][sq] = gen_pawn_attacks(BLACK, sq);

    // Board edges are not considered in the relevant occupancies
    Bitboard edges = ((Rank1BB | Rank8BB) & ~rank_bb(sq)) | ((FILE_ABB | FILE_HBB) & ~file_bb(sq));

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
  

  memset(LineBB, 0, sizeof(LineBB));
  memset(BetweenBB, 0, sizeof(BetweenBB));

  for (Square s1 = SQ_A1; s1 < SQUARE_NB; ++s1) {
    for (Square s2 = SQ_A1; s2 < SQUARE_NB; ++s2) {
      if (get_bishop_attacks(s1) & s2) {
        BetweenBB[s1][s2] = get_bishop_attacks(s1, square_bb(s2)) & get_bishop_attacks(s2, square_bb(s1));

        LineBB[s1][s2] = (get_bishop_attacks(s1) & get_bishop_attacks(s2)) | s1 | s2;
      }
      else  if (get_rook_attacks(s1) & s2) {
        BetweenBB[s1][s2] = get_rook_attacks(s1, square_bb(s2)) & get_rook_attacks(s2, square_bb(s1));

        LineBB[s1][s2] = (get_rook_attacks(s1) & get_rook_attacks(s2)) | s1 | s2;
      }
      BetweenBB[s1][s2] |= s2;
    }
  }
}