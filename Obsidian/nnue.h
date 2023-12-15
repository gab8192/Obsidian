#pragma once

#include "simd.h"
#include "types.h"

#define EvalFile "fireandice181.bin"

using namespace SIMD;

namespace NNUE {

  using weight_t = int16_t;

  constexpr int FeatureDimensions = 768;
  constexpr int TransformedFeatureDimensions = 1536;

  constexpr int NetworkScale = 400;
  constexpr int NetworkQ = 255 * 64;

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
