#pragma once

#include "simd.h"
#include "types.h"

#define EvalFile "net7.bin"

using namespace SIMD;

namespace NNUE {

  using weight_t = int16_t;

  constexpr int FeatureDimensions = 768;
  constexpr int TransformedFeatureDimensions = 1536;

  constexpr int NetworkScale = 400;
  constexpr int NetworkQA = 181;
  constexpr int NetworkQB = 64;
  constexpr int NetworkQAB = NetworkQA * NetworkQB;

  struct Accumulator {
    alignas(Alignment) weight_t white[TransformedFeatureDimensions];
    alignas(Alignment) weight_t black[TransformedFeatureDimensions];

    void reset();

    void activateFeature(Square sq, Piece pc);

    void deactivateFeature(Square sq, Piece pc);

    void moveFeature(Square from, Square to, Piece pc);
  };

  void load();

  Score evaluate(Accumulator& accumulator, Color sideToMove);
}
