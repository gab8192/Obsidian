#pragma once

#include "simd.h"
#include "types.h"

#define EvalFile "net7-epoch5.bin"

using namespace SIMD;

namespace NNUE {

  using weight_t = int16_t;

  constexpr int FeatureDimensions = 768;
  constexpr int TransformedFeatureDimensions = 1024;

  constexpr int NetworkScale = 400;
  constexpr int NetworkQ = 255 * 64;

  constexpr int KingBuckets[SQUARE_NB] = {
    0, 0, 1, 1, 1, 1, 0, 0,
    0, 0, 1, 1, 1, 1, 0, 0,
    2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2
  };

  constexpr int KingBucketsNB = 3;

  struct Accumulator {
    alignas(Alignment) weight_t white[TransformedFeatureDimensions];
    alignas(Alignment) weight_t black[TransformedFeatureDimensions];

    void reset();

    void activateFeature(Square sq, Piece pc, Square wKing, Square bKing);

    void deactivateFeature(Square sq, Piece pc, Square wKing, Square bKing);

    void moveFeature(Square from, Square to, Piece pc, Square wKing, Square bKing);
  };

  void load();

  Score evaluate(Accumulator& accumulator, Color sideToMove);
}
