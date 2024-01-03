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
  return bb & (-int64_t(bb));
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

constexpr bool more_than_one(Bitboard bb) {
  return bb & (bb - 1);
}

inline Bitboard file_bb(Square sqr) {
  return FILES_BB[sqr & 0x7];
}

inline Bitboard rank_bb(Square sqr) {
  return RANKS_BB[sqr >> 3];
}

void printBitboard(Bitboard bitboard);

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

inline Bitboard get_piece_attacks(Piece pc, Square s, Bitboard occupied) {
  switch (ptypeOf(pc)) {
  case PAWN:   return get_pawn_attacks(s, colorOf(pc));
  case KNIGHT: return get_knight_attacks(s);
  case BISHOP: return get_bishop_attacks(s, occupied);
  case ROOK:   return get_rook_attacks(s, occupied);
  case QUEEN:  return get_queen_attacks(s, occupied);
  case KING:   return get_king_attacks(s);
  }
  return 0;
}

void bitboardsInit();