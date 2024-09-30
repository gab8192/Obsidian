#pragma once

#include "types.h"

#include <immintrin.h>

inline Square getLsb(Bitboard bb) {
  return Square(__builtin_ctzll(bb));
}

inline Square popLsb(Bitboard& bbPtr) {
  Square taken = getLsb(bbPtr);
  bbPtr &= (bbPtr - 1);
  return taken;
}

inline Bitboard getLsb_bb(Bitboard bb) {
  return bb & (-int64_t(bb));
}

inline Bitboard squareBB(Square sq) {
  return 1ULL << sq;
}

inline Bitboard operator&(Bitboard d1, Square d2) { return d1 & squareBB(d2); }
inline Bitboard& operator&=(Bitboard& d1, Square d2) { return d1 = d1 & d2; }

inline Bitboard operator|(Bitboard d1, Square d2) { return d1 | squareBB(d2); }
inline Bitboard& operator|=(Bitboard& d1, Square d2) { return d1 = d1 | d2; }

inline Bitboard operator^(Bitboard d1, Square d2) { return d1 ^ squareBB(d2); }
inline Bitboard& operator^=(Bitboard& d1, Square d2) { return d1 = d1 ^ d2; }

inline Bitboard operator|(Square d1, Square d2) { return squareBB(d1) | squareBB(d2); }

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

extern Bitboard BETWEEN_BB[SQUARE_NB][SQUARE_NB];
extern Bitboard LINE_BB[SQUARE_NB][SQUARE_NB];

constexpr bool moreThanOne(Bitboard bb) {
  return bb & (bb - 1);
}

inline Bitboard fileBB(Square sqr) {
  return FILES_BB[sqr & 0x7];
}

inline Bitboard rankBB(Square sqr) {
  return RANKS_BB[sqr >> 3];
}

void printBitboard(Bitboard bitboard);

Bitboard getBishopAttacks(Square s, Bitboard occupied) ;

Bitboard getBishopAttacks(Square s);

Bitboard getRookAttacks(Square s, Bitboard occupied);

Bitboard getRookAttacks(Square s);

Bitboard getQueenAttacks(Square s, Bitboard occupied);

Bitboard getKingAttacks(Square square);

Bitboard getKnightAttacks(Square square);

Bitboard getPawnAttacks(Square square, Color pawnColor);

Bitboard getPawnBbAttacks(Bitboard pawns, Color pawnColor);

Bitboard getPieceAttacks(PieceType pt, Square s, Bitboard occupied);

// rook magic numbers
constexpr Bitboard ROOK_MAGICS[64] = {
    0xa8002c000108020ULL,
    0x6c00049b0002001ULL,
    0x100200010090040ULL,
    0x2480041000800801ULL,
    0x280028004000800ULL,
    0x900410008040022ULL,
    0x280020001001080ULL,
    0x2880002041000080ULL,
    0xa000800080400034ULL,
    0x4808020004000ULL,
    0x2290802004801000ULL,
    0x411000d00100020ULL,
    0x402800800040080ULL,
    0xb000401004208ULL,
    0x2409000100040200ULL,
    0x1002100004082ULL,
    0x22878001e24000ULL,
    0x1090810021004010ULL,
    0x801030040200012ULL,
    0x500808008001000ULL,
    0xa08018014000880ULL,
    0x8000808004000200ULL,
    0x201008080010200ULL,
    0x801020000441091ULL,
    0x800080204005ULL,
    0x1040200040100048ULL,
    0x120200402082ULL,
    0xd14880480100080ULL,
    0x12040280080080ULL,
    0x100040080020080ULL,
    0x9020010080800200ULL,
    0x813241200148449ULL,
    0x491604001800080ULL,
    0x100401000402001ULL,
    0x4820010021001040ULL,
    0x400402202000812ULL,
    0x209009005000802ULL,
    0x810800601800400ULL,
    0x4301083214000150ULL,
    0x204026458e001401ULL,
    0x40204000808000ULL,
    0x8001008040010020ULL,
    0x8410820820420010ULL,
    0x1003001000090020ULL,
    0x804040008008080ULL,
    0x12000810020004ULL,
    0x1000100200040208ULL,
    0x430000a044020001ULL,
    0x280009023410300ULL,
    0xe0100040002240ULL,
    0x200100401700ULL,
    0x2244100408008080ULL,
    0x8000400801980ULL,
    0x2000810040200ULL,
    0x8010100228810400ULL,
    0x2000009044210200ULL,
    0x4080008040102101ULL,
    0x40002080411d01ULL,
    0x2005524060000901ULL,
    0x502001008400422ULL,
    0x489a000810200402ULL,
    0x1004400080a13ULL,
    0x4000011008020084ULL,
    0x26002114058042ULL,
};

// bishop magic number
constexpr Bitboard BISHOP_MAGICS[64] = {
    0x89a1121896040240ULL,
    0x2004844802002010ULL,
    0x2068080051921000ULL,
    0x62880a0220200808ULL,
    0x4042004000000ULL,
    0x100822020200011ULL,
    0xc00444222012000aULL,
    0x28808801216001ULL,
    0x400492088408100ULL,
    0x201c401040c0084ULL,
    0x840800910a0010ULL,
    0x82080240060ULL,
    0x2000840504006000ULL,
    0x30010c4108405004ULL,
    0x1008005410080802ULL,
    0x8144042209100900ULL,
    0x208081020014400ULL,
    0x4800201208ca00ULL,
    0xf18140408012008ULL,
    0x1004002802102001ULL,
    0x841000820080811ULL,
    0x40200200a42008ULL,
    0x800054042000ULL,
    0x88010400410c9000ULL,
    0x520040470104290ULL,
    0x1004040051500081ULL,
    0x2002081833080021ULL,
    0x400c00c010142ULL,
    0x941408200c002000ULL,
    0x658810000806011ULL,
    0x188071040440a00ULL,
    0x4800404002011c00ULL,
    0x104442040404200ULL,
    0x511080202091021ULL,
    0x4022401120400ULL,
    0x80c0040400080120ULL,
    0x8040010040820802ULL,
    0x480810700020090ULL,
    0x102008e00040242ULL,
    0x809005202050100ULL,
    0x8002024220104080ULL,
    0x431008804142000ULL,
    0x19001802081400ULL,
    0x200014208040080ULL,
    0x3308082008200100ULL,
    0x41010500040c020ULL,
    0x4012020c04210308ULL,
    0x208220a202004080ULL,
    0x111040120082000ULL,
    0x6803040141280a00ULL,
    0x2101004202410000ULL,
    0x8200000041108022ULL,
    0x21082088000ULL,
    0x2410204010040ULL,
    0x40100400809000ULL,
    0x822088220820214ULL,
    0x40808090012004ULL,
    0x910224040218c9ULL,
    0x402814422015008ULL,
    0x90014004842410ULL,
    0x1000042304105ULL,
    0x10008830412a00ULL,
    0x2520081090008908ULL,
    0x40102000a0a60140ULL,
};

namespace Bitboards {
  void init();
}