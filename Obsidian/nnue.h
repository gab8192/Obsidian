#pragma once

#include "types.h"

#ifdef USE_AVX2
  #include <immintrin.h>
#endif

namespace NNUE {

  constexpr int FeatureDimensions = 768;
  constexpr int TransformedFeatureDimensions = 384;

  struct Accumulator {
	int16_t white[TransformedFeatureDimensions];
	int16_t black[TransformedFeatureDimensions];

	void reset();

	void activateFeature(Square sq, Piece pc);

	void deactivateFeature(Square sq, Piece pc);

	void moveFeature(Square from, Square to, Piece pc);
  };

  void load(const char* file);

  Value evaluate(Accumulator* accumulator, Color sideToMove);
}
