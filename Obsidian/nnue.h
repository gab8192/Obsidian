#pragma once

#include "simd.h"
#include "types.h"

#define EvalFile "net10-epoch6.bin"

using namespace SIMD;

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

struct Position;

namespace NNUE {

  using weight_t = int16_t;

  constexpr int FeaturesWidth = 768;
  constexpr int HiddenWidth = 1536;

  constexpr int OutputBucketCount = 9;

  constexpr int NetworkScale = 400;
  constexpr int NetworkQA = 255;
  constexpr int NetworkQB = 64;
  constexpr int NetworkQAB = NetworkQA * NetworkQB;

  struct Accumulator {
    union {
      alignas(Alignment) weight_t colors[COLOR_NB][HiddenWidth];
      alignas(Alignment) weight_t both[COLOR_NB * HiddenWidth];
    };

    void reset();

    void addPiece(Square sq, Piece pc);

    void movePiece(Square from, Square to, Piece pc);

    void doUpdates(DirtyPieces& dp, Accumulator& input);
  };

  void init();

  Score evaluate(Position& pos, Accumulator& accumulator);
}
