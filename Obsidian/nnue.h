#pragma once

#ifdef USE_AVX2
  #include <immintrin.h>
#endif

#include "types.h"

namespace NNUE {

  constexpr int FeatureDimensions = 768;
  constexpr int TransformedFeatureDimensions = 256;

  struct Accumulator {
	int16_t white[TransformedFeatureDimensions];
	int16_t black[TransformedFeatureDimensions];

	void reset();

	void activateFeature(Square sq, Piece pc);

	void deactivateFeature(Square sq, Piece pc);

	void moveFeature(Square from, Square to, Piece pc);
  };

  void load(const char* file);

  Value evaluate();
}