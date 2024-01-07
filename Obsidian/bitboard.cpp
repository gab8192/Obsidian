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
  cout << endl;
  // loop over board ranks
  for (Rank y = RANK_8; y >= RANK_1; --y)
  {
    // print rank
    cout << "  " << (y + 1) << " ";

    // loop over board files
    for (File x = FILE_A; x < FILE_NB; ++x)
    {
      // init board square
      Square square = make_square(x, y);

      // print bit indexed by board square
      cout << " " << (bitboard & square ? 1 : 0);
    }

    cout << endl;
  }

  // print files
  cout << "\n     a b c d e f g h" << endl;

  // print bitboard as decimal
  cout << "     bitboard: " << bitboard << endl;
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

Bitboard mask_king_attacks(Square sqr) {
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

Bitboard mask_knight_attacks(Square sqr) {
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
Bitboard mask_pawn_attacks(Color pawnColor, Square sqr) {
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

uint32_t bmi2_index_bishop(Square s, Bitboard occupied)
{
  return (uint32_t)_pext_u64(occupied, BishopMasks[s]);
}

uint32_t bmi2_index_rook(Square s, Bitboard occupied)
{
  return (uint32_t)_pext_u64(occupied, RookMasks[s]);
}

// lookup bishop attacks 
Bitboard get_bishop_attacks(Square square, Bitboard occupancy) {
	
  int index;

#if defined(USE_PEXT)
  index = _pext_u64(occupancy, BishopMasks[square]);
#else
  occupancy &= BishopMasks[square];
	occupancy *=  BishopMagics[square];
	index = occupancy >> 64 - BishopRelevantBits[square];
#endif
	
	return BishopAttacks[square][index];
	
}

// lookup rook attacks 
Bitboard get_rook_attacks(Square square, Bitboard occupancy) {
	
  int index;

#if defined(USE_PEXT)
  index = _pext_u64(occupancy, RookMasks[square]);
#else
  occupancy &= RookMasks[square];
	occupancy *=  rook_magics[square];
	occupancy >>= 64 - RookRelevantBits[square];
#endif
  
	return RookAttacks[square][index];
}

#if defined(USE_PEXT)

void init_attacks(Bitboard table[], Bitboard* attacks[], Bitboard masks[],
  const Direction deltas[], AttackIndexFunc index)
{
  
  for (Square sq = SQ_A1; sq < SQUARE_NB; ++sq) {
    attacks[sq] = table;

    // Board edges are not considered in the relevant occupancies
    Bitboard edges = ((Rank1BB | Rank8BB) & ~rank_bb(sq)) | ((FILE_ABB | FILE_HBB) & ~file_bb(sq));

    Bitboard mask = masks[sq] = sliding_attack(deltas, sq, 0) & ~edges;

    // Use Carry-Rippler trick to enumerate all subsets of masks[s] and
    // fill the attacks table.
    Bitboard b = 0;
    do {
      attacks[sq][index(sq, b)] = sliding_attack(deltas, sq, b);
      b = (b - mask) & mask;
      table++;
    } while (b);
  }
}

#else

// init slider pieces attacks
void init_attacks(Bitboard masks[],
  const Direction* deltas, PieceType pt)
{
    // loop over 64 board squares
    for (Square sq = SQ_A1; sq < SQUARE_NB; ++sq)
    {
        Bitboard edges = ((Rank1BB | Rank8BB) & ~rank_bb(sq)) | ((FILE_ABB | FILE_HBB) & ~file_bb(sq));

        // init bishop & rook masks
        Bitboard mask = masks[sq] = sliding_attack(deltas, sq, 0) & ~edges;
        
        // count attack mask bits
        int bit_count = BitCount(mask);
        
        // occupancy variations count
        int occupancy_variations = 1 << bit_count;
        
        // loop over occupancy variations
        for (int count = 0; count < occupancy_variations; count++)
        {
            Bitboard occupancy = set_occupancy(count, bit_count, mask);

            if (pt == BISHOP) {
              Bitboard magic_index = occupancy * BishopMagics[sq] >> 64 - BishopRelevantBits[sq]; 
              BishopAttacks[sq][magic_index] = sliding_attack(deltas, sq, occupancy); 
            } else {
              Bitboard magic_index = occupancy * rook_magics[sq] >> 64 - RookRelevantBits[sq]; 
              RookAttacks[sq][magic_index] = sliding_attack(deltas, sq, occupancy); 
            }
        }
    }
}

#endif

void bitboardsInit() {
  for (Square sqr = SQ_A1; sqr < SQUARE_NB; ++sqr) {
    king_attacks[sqr] = mask_king_attacks(sqr);

    knight_attacks[sqr] = mask_knight_attacks(sqr);

    pawn_attacks[WHITE][sqr] = mask_pawn_attacks(WHITE, sqr);
    pawn_attacks[BLACK][sqr] = mask_pawn_attacks(BLACK, sqr);

  }

  // Init sliding attacks

  #if defined(USE_PEXT)
  init_attacks(RookTable, RookAttacks, RookMasks, RookDirs, bmi2_index_rook);
  init_attacks(BishopTable, BishopAttacks, BishopMasks, BishopDirs, bmi2_index_bishop);
  #else
  init_attacks(BishopMasks, BishopDirs, BISHOP); // bishop
  init_attacks(RookMasks, RookDirs, ROOK); // rook
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