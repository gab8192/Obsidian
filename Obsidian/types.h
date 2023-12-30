#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <nmmintrin.h>
#include <thread>

using namespace std;

const string engineVersion = "dev-9.16";

using Key = uint64_t;
using Bitboard = uint64_t;
using TbResult = uint32_t;

const string piecesChar = " PNBRQK  pnbrqk";

constexpr int MAX_PLY = 246;
constexpr int MAX_MOVES = 224; // 32*7

#define BitCount(x) _mm_popcnt_u64(x)

inline void sleep(int millis) {
  this_thread::sleep_for(chrono::milliseconds(millis));
}

inline int64_t timeMillis() {

  auto sinceEpoch = chrono::steady_clock::now().time_since_epoch();

  return chrono::duration_cast<chrono::milliseconds>(sinceEpoch).count();
}

enum Score : int {
  DRAW = 0,
  CHECKMATE = 32000,
  SCORE_INFINITE = 32001,
  SCORE_NONE = 32002,

  TB_WIN_IN_MAX_PLY = CHECKMATE - 2 * MAX_PLY,
  TB_LOSS_IN_MAX_PLY = -TB_WIN_IN_MAX_PLY,
  
  CHECKMATE_IN_MAX_PLY = CHECKMATE - MAX_PLY,
  CHECKMATED_IN_MAX_PLY = -CHECKMATE_IN_MAX_PLY

};

enum Move {
  MOVE_NONE = 0
};

enum MoveType {
  MT_NORMAL, MT_CASTLING, MT_EN_PASSANT, MT_PROMOTION,
};


enum Square : int {
  SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
  SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
  SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
  SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
  SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
  SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
  SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
  SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
  SQ_NONE,

  SQUARE_NB = 64
};

enum Direction : int {
  NORTH = 8,
  EAST = 1,
  SOUTH = -NORTH,
  WEST = -EAST,

  NORTH_EAST = NORTH + EAST,
  SOUTH_EAST = SOUTH + EAST,
  SOUTH_WEST = SOUTH + WEST,
  NORTH_WEST = NORTH + WEST
};

enum Rank : int {
  RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8,
  RANK_NB = 8
};

enum File : int {
  FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H,
  FILE_NB = 8
};

enum CastlingRights {
  NO_CASTLING,
  WHITE_OO,
  WHITE_OOO = WHITE_OO << 1,
  BLACK_OO = WHITE_OO << 2,
  BLACK_OOO = WHITE_OO << 3,

  SHORT_CASTLING = WHITE_OO | BLACK_OO,
  LONG_CASTLING = WHITE_OOO | BLACK_OOO,

  WHITE_CASTLING = WHITE_OO | WHITE_OOO,
  BLACK_CASTLING = BLACK_OO | BLACK_OOO,

  ALL_CASTLING = WHITE_CASTLING | BLACK_CASTLING
};

struct CastlingData {
  Square kingSrc, kingDest, rookSrc, rookDest;
};

constexpr CastlingData CASTLING_DATA[9] = {
    {},
    {SQ_E1, SQ_G1, SQ_H1, SQ_F1}, // WHITE_OO
    {SQ_E1, SQ_C1, SQ_A1, SQ_D1}, // WHITE_OOO
    {},
    {SQ_E8, SQ_G8, SQ_H8, SQ_F8}, // BLACK_OO
    {}, {}, {},
    {SQ_E8, SQ_C8, SQ_A8, SQ_D8} // BLACK_OOO
};

constexpr Bitboard CASTLING_PATH[9] = {
    0,
    (1ULL << SQ_F1) | (1ULL << SQ_G1), // WHITE_OO
    (1ULL << SQ_B1) | (1ULL << SQ_C1) | (1ULL << SQ_D1), // WHITE_OOO
    0,
    (1ULL << SQ_F8) | (1ULL << SQ_G8), // BLACK_OO
    0, 0, 0,
    (1ULL << SQ_B8) | (1ULL << SQ_C8) | (1ULL << SQ_D8) // BLACK_OOO
};

enum Color : int {
  WHITE, BLACK, COLOR_NB = 2
};

enum PieceType : int {
  NO_PIECE_TYPE, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, ALL_PIECES,
  PIECE_TYPE_NB = 8
};

constexpr Square make_square(File x, Rank y) {
  return Square(x | (y << 3));
}

constexpr File file_of(Square sqr) {
  return File(sqr & 0x7);
}

constexpr Rank rank_of(Square sqr) {
  return Rank(sqr >> 3);
}

constexpr Square relative_square(Color color, Square s) {
  return Square(s ^ (color * 56));
}

constexpr Rank relative_rank(Color color, Rank r) {
  return Rank(r ^ (color * 7));
}

constexpr Rank relative_rank(Color color, Square s) {
  return relative_rank(color, rank_of(s));
}

enum Piece : char;

constexpr Piece make_piece(Color color, PieceType pt) {
  return Piece((color << 3) + pt);
}

constexpr PieceType ptypeOf(Piece piece) {
  
  return PieceType(piece & 7);
}

constexpr Color colorOf(Piece piece) {
  return Color(piece >> 3);
}

enum Piece : char {
  NO_PIECE,

  W_PAWN = make_piece(WHITE, PAWN),
  W_KNIGHT = make_piece(WHITE, KNIGHT),
  W_BISHOP = make_piece(WHITE, BISHOP),
  W_ROOK = make_piece(WHITE, ROOK),
  W_QUEEN = make_piece(WHITE, QUEEN),
  W_KING = make_piece(WHITE, KING),
  
  B_PAWN = make_piece(BLACK, PAWN),
  B_KNIGHT = make_piece(BLACK, KNIGHT),
  B_BISHOP = make_piece(BLACK, BISHOP),
  B_ROOK = make_piece(BLACK, ROOK),
  B_QUEEN = make_piece(BLACK, QUEEN),
  B_KING = make_piece(BLACK, KING),

  PIECE_NB = 16
};

// These defines are copied from stockfish types.h

#define ENABLE_BASE_OPERATORS_ON(T)                                \
inline T operator+(T d1, int d2) { return T(int(d1) + d2); }    \
inline T operator-(T d1, int d2) { return T(int(d1) - d2); }    \
inline T operator-(T d) { return T(-int(d)); }                  \
inline T& operator+=(T& d1, int d2) { return d1 = d1 + d2; }       \
inline T& operator-=(T& d1, int d2) { return d1 = d1 - d2; }

#define ENABLE_LOGIC_OPERATORS_ON(T)                               \
inline T operator~(T d1) { return T(~ int(d1)); }    \
inline T operator&(T d1, int d2) { return T(int(d1) & d2); }    \
inline T& operator&=(T& d1, int d2) { return d1 = d1 & d2; }    \
inline T operator|(T d1, int d2) { return T(int(d1) | d2); }    \
inline T& operator|=(T& d1, int d2) { return d1 = d1 | d2; }

#define ENABLE_INCR_OPERATORS_ON(T)                                \
inline T& operator++(T& d) { return d = T(int(d) + 1); }           \
inline T& operator--(T& d) { return d = T(int(d) - 1); }


ENABLE_BASE_OPERATORS_ON(File)
ENABLE_BASE_OPERATORS_ON(Rank)
ENABLE_BASE_OPERATORS_ON(Square)
ENABLE_BASE_OPERATORS_ON(Score)

ENABLE_INCR_OPERATORS_ON(File)
ENABLE_INCR_OPERATORS_ON(Rank)
ENABLE_INCR_OPERATORS_ON(Square)

ENABLE_LOGIC_OPERATORS_ON(CastlingRights)


constexpr Color operator~(Color d1) { return Color(int(d1) ^ 1); }

constexpr int PieceValue[PIECE_NB] = { 0, 100, 370, 390, 610, 1210, 0, 0,
                                       0, 100, 370, 390, 610, 1210, 0, 0 };

inline std::ostream& operator<<(std::ostream& stream, Color color) {
  stream << (color == WHITE ? "white" : "black");
  return stream;
}
