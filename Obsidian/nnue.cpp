#include "nnue.h"
#include "bitboard.h"
#include "incbin.h"

#include <iostream>
#include <fstream>

#ifndef _MSC_VER
INCBIN(EmbeddedNNUE, EvalFile);
#endif

using namespace std;

namespace NNUE {

#if defined(USE_AVX512)

  using Vec = __m512i;

  inline Vec vecAddEpi16(Vec x, Vec y) {
    return _mm512_add_epi16(x, y);
  }

  inline Vec vecSubEpi16(Vec x, Vec y) {
    return _mm512_sub_epi16(x, y);
  }

  inline int vecHadd(Vec vec) {
    return _mm512_reduce_add_epi32(vec);
  }

#elif defined(USE_AVX2)

  using Vec = __m256i;

  inline Vec vecAddEpi16(Vec x, Vec y) {
    return _mm256_add_epi16(x, y);
  }

  inline Vec vecSubEpi16(Vec x, Vec y) {
    return _mm256_sub_epi16(x, y);
  }

  inline int vecHadd(Vec vec) {
    __m128i xmm0;
    __m128i xmm1;

    // Get the lower and upper half of the register:
    xmm0 = _mm256_castsi256_si128(vec);
    xmm1 = _mm256_extracti128_si256(vec, 1);

    // Add the lower and upper half vertically:
    xmm0 = _mm_add_epi32(xmm0, xmm1);

    // Get the upper half of the result:
    xmm1 = _mm_unpackhi_epi64(xmm0, xmm0);

    // Add the lower and upper half vertically:
    xmm0 = _mm_add_epi32(xmm0, xmm1);

    // Shuffle the result so that the lower 32-bits are directly above the second-lower 32-bits:
    xmm1 = _mm_shuffle_epi32(xmm0, _MM_SHUFFLE(2, 3, 0, 1));

    // Add the lower 32-bits to the second-lower 32-bits vertically:
    xmm0 = _mm_add_epi32(xmm0, xmm1);

    // Cast the result to the 32-bit integer type and return it:
    return _mm_cvtsi128_si32(xmm0);
  }

#else

  using Vec = weight_t;

  inline Vec vecAddEpi16(Vec x, Vec y) {
    return x + y;
  }

  inline Vec vecSubEpi16(Vec x, Vec y) {
    return x - y;
  }

#endif

  constexpr int WeightsPerVec = sizeof(Vec) / sizeof(weight_t);

  alignas(SimdAlign) int FeatureIndexTable[COLOR_NB][PIECE_NB][SQUARE_NB];

  struct {
    alignas(SimdAlign) weight_t FeatureWeights[FeatureDimensions * TransformedFeatureDimensions];
    alignas(SimdAlign) weight_t FeatureBiases[TransformedFeatureDimensions];
    alignas(SimdAlign) weight_t OutputWeights[2 * TransformedFeatureDimensions];
                       weight_t OutputBias;
  } Content;

  template <int InputSize>
  inline void addToAll(weight_t* input, int offset)
  {
    offset /= WeightsPerVec;

    Vec* inputVec = (Vec*)input;
    Vec* weightsVec = (Vec*)Content.FeatureWeights;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i) {
      inputVec[i] = vecAddEpi16(inputVec[i], weightsVec[offset + i]);
    }
  }

  template <int InputSize>
  inline void subtractFromAll(weight_t* input, int offset)
  {
    offset /= WeightsPerVec;

    Vec* inputVec = (Vec*)input;
    Vec* weightsVec = (Vec*)Content.FeatureWeights;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i) {
      inputVec[i] = vecSubEpi16(inputVec[i], weightsVec[offset + i]);
    }
  }

  template <int InputSize>
  inline void addAndSubtractFromAll(weight_t* input, int addOff, int subtractOff) {
    addOff /= WeightsPerVec;
    subtractOff /= WeightsPerVec;

    Vec* inputVec = (Vec*)input;
    Vec* weightsVec = (Vec*)Content.FeatureWeights;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i) {
      inputVec[i] = vecSubEpi16(vecAddEpi16(inputVec[i], weightsVec[addOff + i]), weightsVec[subtractOff + i]);
    }
  }


  void Accumulator::reset() {
    memcpy(white, Content.FeatureBiases, sizeof(Content.FeatureBiases));
    memcpy(black, Content.FeatureBiases, sizeof(Content.FeatureBiases));
  }

  void Accumulator::activateFeature(Square sq, Piece pc) {
    addToAll<TransformedFeatureDimensions>(white, FeatureIndexTable[WHITE][pc][sq]);

    addToAll<TransformedFeatureDimensions>(black, FeatureIndexTable[BLACK][pc][sq]);
  }

  void Accumulator::deactivateFeature(Square sq, Piece pc) {
    subtractFromAll<TransformedFeatureDimensions>(white, FeatureIndexTable[WHITE][pc][sq]);

    subtractFromAll<TransformedFeatureDimensions>(black, FeatureIndexTable[BLACK][pc][sq]);
  }

  void Accumulator::moveFeature(Square from, Square to, Piece pc) {
    addAndSubtractFromAll<TransformedFeatureDimensions>(white,
      FeatureIndexTable[WHITE][pc][to], FeatureIndexTable[WHITE][pc][from]);

    addAndSubtractFromAll<TransformedFeatureDimensions>(black,
      FeatureIndexTable[BLACK][pc][to], FeatureIndexTable[BLACK][pc][from]);
  }

  void load() {
#ifdef _MSC_VER
    ifstream stream(EvalFile, ios::binary);

    if (!bool(stream)) {
      cout << "Failed to load NNUE" << endl;
      exit(1);
    }

    stream.read((char*)&Content, sizeof(Content));
#else
    memcpy(&Content, gEmbeddedNNUEData, sizeof(Content));
#endif // _MSC_VER	

    // Cache feature indexes
    for (int c = 0; c <= 1; ++c) {
      for (int pt = PAWN; pt <= KING; ++pt) {
        for (Square sq = SQ_A1; sq < SQUARE_NB; ++sq) {
          Color color = Color(c);
          Piece piece = make_piece(color, PieceType(pt));

          FeatureIndexTable[color][piece][sq] =
            SQUARE_NB * (pt - 1) + relative_square(color, sq);

          FeatureIndexTable[~color][piece][sq] =
            SQUARE_NB * (pt + 5) + relative_square(~color, sq);

          FeatureIndexTable[color][piece][sq] *= TransformedFeatureDimensions;
          FeatureIndexTable[~color][piece][sq] *= TransformedFeatureDimensions;
        }
      }
    }
  }

  inline int clippedRelu(weight_t x) {
    if (x < 0)
      return 0;
    if (x > 255)
      return 255;
    return x;
  }

  Value evaluate(Accumulator& accumulator, Color sideToMove) {

    weight_t* stmAccumulator;
    weight_t* oppAccumulator;

    if (sideToMove == WHITE) {
      stmAccumulator = accumulator.white;
      oppAccumulator = accumulator.black;
    }
    else {
      stmAccumulator = accumulator.black;
      oppAccumulator = accumulator.white;
    }

    int sum = Content.OutputBias;

#if defined(USE_AVX512)

    const Vec reluClipMin = _mm512_setzero_si512();
    const Vec reluClipMax = _mm512_set1_epi16(255);

    Vec* stmAccVec = (Vec*)stmAccumulator;
    Vec* oppAccVec = (Vec*)oppAccumulator;
    Vec* stmWeightsVec = (Vec*)Content.OutputWeights;
    Vec* oppWeightsVec = (Vec*)&Content.OutputWeights[TransformedFeatureDimensions];

    Vec sumVec = _mm512_setzero_si512();

    for (int i = 0; i < TransformedFeatureDimensions / WeightsPerVec; ++i) {

      { // Side to move
        Vec crelu = _mm512_min_epi16(_mm512_max_epi16(stmAccVec[i], reluClipMin), reluClipMax);
        Vec stmProduct = _mm512_madd_epi16(crelu, stmWeightsVec[i]);
        sumVec = _mm512_add_epi32(sumVec, stmProduct);
      }
      { // Non side to move
        Vec crelu = _mm512_min_epi16(_mm512_max_epi16(oppAccVec[i], reluClipMin), reluClipMax);
        Vec oppProduct = _mm512_madd_epi16(crelu, oppWeightsVec[i]);
        sumVec = _mm512_add_epi32(sumVec, oppProduct);
      }
    }

    sum += vecHadd(sumVec);

#elif defined(USE_AVX2)

    const Vec reluClipMin = _mm256_setzero_si256();
    const Vec reluClipMax = _mm256_set1_epi16(255);

    Vec* stmAccVec = (Vec*)stmAccumulator;
    Vec* oppAccVec = (Vec*)oppAccumulator;
    Vec* stmWeightsVec = (Vec*)Content.OutputWeights;
    Vec* oppWeightsVec = (Vec*)&Content.OutputWeights[TransformedFeatureDimensions];

    Vec sumVec = _mm256_setzero_si256();

    for (int i = 0; i < TransformedFeatureDimensions / WeightsPerVec; ++i) {

      { // Side to move
        Vec crelu = _mm256_min_epi16(_mm256_max_epi16(stmAccVec[i], reluClipMin), reluClipMax);
        Vec stmProduct = _mm256_madd_epi16(crelu, stmWeightsVec[i]);
        sumVec = _mm256_add_epi32(sumVec, stmProduct);
      }
      { // Non side to move
        Vec crelu = _mm256_min_epi16(_mm256_max_epi16(oppAccVec[i], reluClipMin), reluClipMax);
        Vec oppProduct = _mm256_madd_epi16(crelu, oppWeightsVec[i]);
        sumVec = _mm256_add_epi32(sumVec, oppProduct);
      }
    }

    sum += vecHadd(sumVec);

#else

    for (int i = 0; i < TransformedFeatureDimensions; ++i) {
      sum += clippedRelu(stmAccumulator[i]) * Content.OutputWeights[i];
      sum += clippedRelu(oppAccumulator[i]) * Content.OutputWeights[TransformedFeatureDimensions + i];
    }

#endif // USE_AVX2

    return Value((sum * NetworkScale) / NetworkQ);
  }

}
