#pragma once

#include "simd.h"
#include "types.h"

#define EvalFile "hexadecane_512_v2.net"

using namespace SIMD;

struct Position;

struct SquarePiece {
  Square sq;
  Piece pc;
};

struct DirtyPieces {
  SquarePiece sub0, add0, sub1, add1;

  enum {
    NORMAL, CAPTURE, CASTLING
  } type;
};

namespace NNUE {

  using weight_t = int16_t;

  constexpr int FeaturesWidth = 768;
  constexpr int HiddenWidth = 512;

  constexpr int KingBucketsScheme[] = {
    0,  1,  2,  3,  3,  2,  1,  0,
    4,  5,  6,  7,  7,  6,  5,  4,
    8,  9,  10, 11, 11, 10, 9,  8,
    8,  9,  10, 11, 11, 10, 9,  8,
    12, 12, 13, 13, 13, 13, 12, 12,
    12, 12, 13, 13, 13, 13, 12, 12,
    14, 14, 15, 15, 15, 15, 14, 14,
    14, 14, 15, 15, 15, 15, 14, 14,
  };
  constexpr int KingBucketsCount = 16;

  constexpr int OutputBuckets = 8;

  constexpr int NetworkScale = 400;
  constexpr int NetworkQAB = 128 * 32;

  bool needRefresh(Color side, Square oldKing, Square newKing);

  struct Accumulator {
    union {
      alignas(Alignment) weight_t colors[COLOR_NB][HiddenWidth];
      alignas(Alignment) weight_t both[COLOR_NB * HiddenWidth];
    };

    void addPiece(Square kingSq, Color side, Piece pc, Square sq);

    void removePiece(Square kingSq, Color side, Piece pc, Square sq);

    void doUpdates(Square kingSq, Color side, DirtyPieces& dp, Accumulator& input);

    void reset(Color side);

    void refresh(Position& pos, Color side);
  };

  void init();

  Score evaluate(Accumulator& accumulator, Position& pos);
}