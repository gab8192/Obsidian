#pragma once

#include "types.h"

#define EvalFile "net4.nnue"

namespace NNUE {

#if defined(USE_AVX512)

  constexpr int SimdAlign = 64;

#elif defined(USE_AVX2)

  constexpr int SimdAlign = 32;

#else

  constexpr int SimdAlign = 8;

#endif

  using weight_t = int16_t;

  constexpr int FeatureDimensions = 768;
  constexpr int TransformedFeatureDimensions = 384;

  struct Accumulator {
    alignas(SimdAlign) weight_t white[TransformedFeatureDimensions];
    alignas(SimdAlign) weight_t black[TransformedFeatureDimensions];

    void reset();

    void activateFeature(Square sq, Piece pc);

    void deactivateFeature(Square sq, Piece pc);

    void moveFeature(Square from, Square to, Piece pc);
  };

  void load();

  Value evaluate(Accumulator* accumulator, Color sideToMove);
}
