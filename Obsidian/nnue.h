#pragma once

#include "types.h"

#ifdef USE_AVX2
  #include <immintrin.h>
#endif

#define EvalFile "net4.nnue"

namespace NNUE {

  using weight_t = int16_t;

  constexpr int FeatureDimensions = 768;
  constexpr int TransformedFeatureDimensions = 384;

  struct Accumulator {
	weight_t white[TransformedFeatureDimensions];
	weight_t black[TransformedFeatureDimensions];

	void reset();

	void activateFeature(Square sq, Piece pc);

	void deactivateFeature(Square sq, Piece pc);

	void moveFeature(Square from, Square to, Piece pc);
  };

  void load();

  Value evaluate(Accumulator* accumulator, Color sideToMove);
}
