#pragma once

#include "simd.h"
#include "types.h"

#define EvalFile "net20-240.bin"

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
  constexpr int HiddenWidth = 1024;

  constexpr int KingBucketsScheme[] = {
    0, 1, 2, 3, 3, 2, 1, 0,
    4, 4, 5, 5, 5, 5, 4, 4,
    6, 6, 6, 6, 6, 6, 6, 6, 
    6, 6, 6, 6, 6, 6, 6, 6, 
    7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7
  };
  constexpr int KingBucketsCount = 8;

  constexpr int OutputBuckets = 8;

  constexpr int NetworkScale = 400;
  constexpr int NetworkQA = 255;
  constexpr int NetworkQB = 64;
  constexpr int NetworkQAB = NetworkQA * NetworkQB;

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