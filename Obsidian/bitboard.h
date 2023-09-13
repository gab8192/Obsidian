#pragma once

#include "types.h"

#include <immintrin.h>

inline Square getLsb(Bitboard bb) {
    return Square(_tzcnt_u64(bb));
}

inline Square popLsb(Bitboard& bbPtr) {
    Square taken = getLsb(bbPtr);
    bbPtr &= (bbPtr - 1);
    return taken;
}

inline Bitboard getLsb_bb(Bitboard bb) {
    return bb & ( - __int64(bb));
}

inline Bitboard square_bb(Square sq) {
  return 1ULL << sq;
}

inline Bitboard operator&(Bitboard d1, Square d2) { return d1 & square_bb(d2); }
inline Bitboard& operator&=(Bitboard& d1, Square d2) { return d1 = d1 & d2; }

inline Bitboard operator|(Bitboard d1, Square d2) { return d1 | square_bb(d2); }
inline Bitboard& operator|=(Bitboard& d1, Square d2) { return d1 = d1 | d2; }

inline Bitboard operator^(Bitboard d1, Square d2) { return d1 ^ square_bb(d2); }
inline Bitboard& operator^=(Bitboard& d1, Square d2) { return d1 = d1 ^ d2; }


inline Bitboard operator|(Square d1, Square d2) { return square_bb(d1) | square_bb(d2); }


constexpr Bitboard AllSquares = ~ Bitboard(0);
constexpr Bitboard DARK_SQUARES_BB = 0xAA55AA55AA55AA55ULL;
constexpr Bitboard LIGHT_SQUARES_BB = ~DARK_SQUARES_BB;

constexpr Bitboard Rank1BB = 0xffULL;
constexpr Bitboard Rank2BB = Rank1BB << (8 * 1);
constexpr Bitboard Rank3BB = Rank1BB << (8 * 2);
constexpr Bitboard Rank4BB = Rank1BB << (8 * 3);
constexpr Bitboard Rank5BB = Rank1BB << (8 * 4);
constexpr Bitboard Rank6BB = Rank1BB << (8 * 5);
constexpr Bitboard Rank7BB = Rank1BB << (8 * 6);
constexpr Bitboard Rank8BB = Rank1BB << (8 * 7);

constexpr Bitboard FILE_ABB = 0x101010101010101ULL;
constexpr Bitboard FILE_BBB = FILE_ABB << 1;
constexpr Bitboard FILE_CBB = FILE_ABB << 2;
constexpr Bitboard FILE_DBB = FILE_ABB << 3;
constexpr Bitboard FILE_EBB = FILE_ABB << 4;
constexpr Bitboard FILE_FBB = FILE_ABB << 5;
constexpr Bitboard FILE_GBB = FILE_ABB << 6;
constexpr Bitboard FILE_HBB = FILE_ABB << 7;

constexpr Bitboard FILES_BB[FILE_NB] = { 
    FILE_ABB,
    FILE_BBB,
    FILE_CBB,
    FILE_DBB,
    FILE_EBB,
    FILE_FBB,
    FILE_GBB,
    FILE_HBB };

constexpr Bitboard RANKS_BB[FILE_NB] = {
    Rank1BB,
    Rank2BB,
    Rank3BB,
    Rank4BB,
    Rank5BB,
    Rank6BB,
    Rank7BB,
    Rank8BB };

constexpr Bitboard ADJACENT_FILES_BB[FILE_NB] = {
    FILE_BBB,
    FILE_ABB | FILE_CBB,
    FILE_BBB | FILE_DBB,
    FILE_CBB | FILE_EBB,
    FILE_DBB | FILE_FBB,
    FILE_EBB | FILE_GBB,
    FILE_FBB | FILE_HBB,
    FILE_GBB };

constexpr Bitboard CENTER_BB = (FILE_DBB | FILE_EBB) & (Rank4BB | Rank5BB);

constexpr Bitboard CenterFiles = FILE_CBB | FILE_DBB | FILE_EBB | FILE_FBB;

constexpr Bitboard QUEEN_SIDE_BB = FILE_ABB | FILE_BBB | FILE_CBB | FILE_DBB;
constexpr Bitboard KING_SIDE_BB = FILE_EBB | FILE_FBB | FILE_GBB | FILE_HBB;

constexpr Bitboard KingFlank[FILE_NB] = {
    QUEEN_SIDE_BB ^ FILE_DBB, QUEEN_SIDE_BB, QUEEN_SIDE_BB,
    CenterFiles, CenterFiles,
    KING_SIDE_BB, KING_SIDE_BB, KING_SIDE_BB ^ FILE_EBB
};

constexpr bool more_than_one(Bitboard bb) {
    return bb & (bb - 1);
}

inline int edge_distance(File f) { 
    return myMin(f, File(FILE_H - f)); }

inline int edge_distance(Rank r) { 
    return myMin(r, Rank(RANK_8 - r)); }

inline int edge_distance(Square sqr) { 
    return myMin(edge_distance(file_of(sqr)), edge_distance(rank_of(sqr)));
}

inline Square flip_rank(Square s) { // Swap A1 <-> A8
    return Square(int(s) ^ int(SQ_A8));
}

inline Square flip_file(Square s) { // Swap A1 <-> H1
    return Square(int(s) ^ int(SQ_H1));
}

inline Bitboard file_bb(Square sqr) {
    return FILES_BB[sqr & 0x7];
}

inline Bitboard file_bb(File file) {
    return FILE_ABB << file;
}

inline Bitboard rank_bb(Square sqr) {
    return RANKS_BB[sqr >> 3];
}

typedef int (*SquareConsumer)(Square sq);

inline void printPerSquareInfo(SquareConsumer consumer)
{
  printf("\n");

  // loop over board ranks
  for (Rank y = RANK_8; y >= RANK_1; --y)
  {
    // print rank
    printf("  %d ", y + 1);

    // loop over board files
    for (File x = FILE_A; x < FILE_NB; ++x)
    {
      // init board square
      Square square = make_square(x, y);

      // print bit indexed by board square
      printf(" %d", consumer(square));
    }

    printf("\n");
  }

  // print files
  printf("\n     a b c d e f g h\n\n");

  std::cout << std::endl;
}

void print_bitboard(Bitboard bitboard);

// masks

extern Bitboard RookMasks[SQUARE_NB];
extern Bitboard BishopMasks[SQUARE_NB];
extern Bitboard* RookAttacks[SQUARE_NB];
extern Bitboard* BishopAttacks[SQUARE_NB];

extern Bitboard king_attacks[SQUARE_NB];
extern Bitboard knight_attacks[SQUARE_NB];

/*
* The 2 squares diagonal to the pawn, which he can capture
*/
extern Bitboard pawn_attacks[COLOR_NB][SQUARE_NB];


extern int SquareDistance[SQUARE_NB][SQUARE_NB];
extern int FileDistance[SQUARE_NB][SQUARE_NB];
extern int RankDistance[SQUARE_NB][SQUARE_NB];
extern Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
extern Bitboard LineBB[SQUARE_NB][SQUARE_NB];

inline Bitboard get_bishop_attacks(Square s, Bitboard occupied) {
    return BishopAttacks[s][_pext_u64(occupied, BishopMasks[s])];
}

inline Bitboard get_bishop_attacks(Square s) {
    return BishopAttacks[s][0];
}

inline Bitboard get_rook_attacks(Square s, Bitboard occupied) {
    return RookAttacks[s][_pext_u64(occupied, RookMasks[s])];
}

inline Bitboard get_rook_attacks(Square s) {
    return RookAttacks[s][0];
}

inline Bitboard get_queen_attacks(Square s) {
  return get_bishop_attacks(s) | get_rook_attacks(s);
}

inline Bitboard get_queen_attacks(Square s, Bitboard occupied) {
  return get_bishop_attacks(s, occupied) | get_rook_attacks(s, occupied);
}

inline Bitboard get_king_attacks(Square square) {
    return king_attacks[square];
}

inline Bitboard get_knight_attacks(Square square) {
    return knight_attacks[square];
}

inline Bitboard get_pawn_attacks(Square square, Color pawnColor) {
    return pawn_attacks[pawnColor][square];
}

template<Color PawnColor>
Bitboard get_pawns_bb_attacks(Bitboard bb);

void bitboardsInit();

inline Bitboard shiftEast(Bitboard bb) {
    return (bb & ~FILE_HBB) << 1;
}

inline Bitboard shiftWest(Bitboard bb) {
    return (bb & ~FILE_ABB) >> 1;
}

inline Bitboard adjacent_files_bb(Square s) {
    return ADJACENT_FILES_BB[file_of(s)];
}

// This function is Copied from stockfish bitboard.h
inline Bitboard forward_ranks_bb(Color c, Square s) {
    return c == WHITE ? (~Rank1BB) << (8 * relative_rank(WHITE, s))
                           : (~Rank8BB) >> (8 * relative_rank(BLACK, s));
}

// All the squares in front of a pawn (including adjacent squares)
// Used in trivial endgames
inline Bitboard passed_pawn_span(Color c, Square s) {
  return forward_ranks_bb(c, s) & (file_bb(s) | adjacent_files_bb(s));
}