#pragma once

#include "simd.h"
#include "types.h"

#define EvalFile "net7.bin"

using namespace SIMD;

struct DirtyPiece {
  Square from;
  Square to;
  Piece pc;
};

namespace NNUE {

  using weight_t = int16_t;

  constexpr int FeaturesWidth = 768;
  constexpr int HiddenWidth = 1536;

  constexpr int NetworkScale = 400;
  constexpr int NetworkQA = 181;
  constexpr int NetworkQB = 64;
  constexpr int NetworkQAB = NetworkQA * NetworkQB;

  struct Accumulator {
    alignas(Alignment) weight_t colors[COLOR_NB][HiddenWidth];

    void reset();

    void activateFeature(Square sq, Piece pc, Accumulator* input);

    void deactivateFeature(Square sq, Piece pc, Accumulator* input);

    void moveFeature(Square from, Square to, Piece pc, Accumulator* input);
  };

  void init();

  Score evaluate(Accumulator& accumulator, Color sideToMove);
}
