#include "nnue.h"
#include "bitboard.h"
#include "incbin.h"

#include <iostream>
#include <fstream>

INCBIN(EmbeddedNNUE, EvalFile);

using namespace std;

namespace NNUE {

  constexpr int WeightsPerVec = sizeof(SIMD::Vec) / sizeof(weight_t);

  alignas(SIMD::Alignment) int FeaturesIndex[COLOR_NB][PIECE_NB][SQUARE_NB];

  struct {
    alignas(SIMD::Alignment) weight_t FeatureWeights[FeaturesWidth * HiddenWidth];
    alignas(SIMD::Alignment) weight_t FeatureBiases[HiddenWidth];
    alignas(SIMD::Alignment) weight_t OutputWeights[2 * HiddenWidth];
                             weight_t OutputBias;
  } Content;

  template <int InputSize>
  inline void addAll(weight_t* output, weight_t* input, int offset)
  {
    offset /= WeightsPerVec;

    Vec* inputVec = (Vec*)input;
    Vec* outputVec = (Vec*)output;
    Vec* weightsVec = (Vec*)Content.FeatureWeights;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      outputVec[i] = addEpi16(inputVec[i], weightsVec[offset + i]);
  }

  template <int InputSize>
  inline void subAll(weight_t* output, weight_t* input, int offset)
  {
    offset /= WeightsPerVec;

    Vec* inputVec = (Vec*)input;
    Vec* outputVec = (Vec*)output;
    Vec* weightsVec = (Vec*)Content.FeatureWeights;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      outputVec[i] = subEpi16(inputVec[i], weightsVec[offset + i]);
  }

  template <int InputSize>
  inline void addSubAll(weight_t* output, weight_t* input, int addOff, int subtractOff) {
    addOff /= WeightsPerVec;
    subtractOff /= WeightsPerVec;

    Vec* inputVec = (Vec*)input;
    Vec* outputVec = (Vec*)output;
    Vec* weightsVec = (Vec*)Content.FeatureWeights;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      outputVec[i] = subEpi16(addEpi16(inputVec[i], weightsVec[addOff + i]), weightsVec[subtractOff + i]);
  }


  void Accumulator::reset() {
    for (int c = WHITE; c <= BLACK; ++c)
      memcpy(colors[c], Content.FeatureBiases, sizeof(Content.FeatureBiases));
  }

  void Accumulator::activateFeature(Square sq, Piece pc, Accumulator* input) {
    for (int c = WHITE; c <= BLACK; ++c)
      addAll<HiddenWidth>(colors[c], input->colors[c], FeaturesIndex[c][pc][sq]);
  }

  void Accumulator::deactivateFeature(Square sq, Piece pc, Accumulator* input) {
    for (int c = WHITE; c <= BLACK; ++c)
      subAll<HiddenWidth>(colors[c], input->colors[c], FeaturesIndex[c][pc][sq]);
  }

  void Accumulator::moveFeature(Square from, Square to, Piece pc, Accumulator* input) {
    for (int c = WHITE; c <= BLACK; ++c)
      addSubAll<HiddenWidth>(colors[c], input->colors[c], FeaturesIndex[c][pc][to], FeaturesIndex[c][pc][from]);
  }

  void init() {

    memcpy(&Content, gEmbeddedNNUEData, sizeof(Content));

    // Cache feature indexes
    for (int c = 0; c <= 1; ++c) {
      for (int pt = PAWN; pt <= KING; ++pt) {
        for (Square sq = SQ_A1; sq < SQUARE_NB; ++sq) {
          Color color = Color(c);
          Piece piece = make_piece(color, PieceType(pt));

          FeaturesIndex[color][piece][sq] =
            SQUARE_NB * (pt - 1) + relative_square(color, sq);

          FeaturesIndex[~color][piece][sq] =
            SQUARE_NB * (pt + 5) + relative_square(~color, sq);

          FeaturesIndex[color][piece][sq] *= HiddenWidth;
          FeaturesIndex[~color][piece][sq] *= HiddenWidth;
        }
      }
    }
  }

  Score evaluate(Accumulator& accumulator, Color sideToMove) {

    Vec* stmAcc = (Vec*) accumulator.colors[sideToMove];
    Vec* oppAcc = (Vec*) accumulator.colors[~sideToMove];

    Vec* stmWeightsVec = (Vec*) &Content.OutputWeights[0];
    Vec* oppWeightsVec = (Vec*) &Content.OutputWeights[HiddenWidth];

    const Vec reluClipMin = vecSetZero();
    const Vec reluClipMax = vecSet1Epi16(NetworkQA);

    Vec sum = vecSetZero();
    Vec reg;

#if defined(USE_AVX512)

    for (int i = 0; i < TransformedFeatureDimensions / WeightsPerVec; ++i) {

      // Side to move
      reg = _mm512_min_epi16(_mm512_max_epi16(stmAcc[i], reluClipMin), reluClipMax); // clip
      reg = _mm512_mullo_epi16(reg, reg); // square
      reg = _mm512_madd_epi16(reg, stmWeightsVec[i]); // multiply with output layer
      sum = _mm512_add_epi32(sum, reg); // collect the result,

      // Non side to move
      reg = _mm512_min_epi16(_mm512_max_epi16(oppAcc[i], reluClipMin), reluClipMax);
      reg = _mm512_mullo_epi16(reg, reg);
      reg = _mm512_madd_epi16(reg, oppWeightsVec[i]);
      sum = _mm512_add_epi32(sum, reg);
    }

#elif defined(USE_AVX2)

    for (int i = 0; i < HiddenWidth / WeightsPerVec; ++i) {

      // Side to move
      reg = _mm256_min_epi16(_mm256_max_epi16(stmAcc[i], reluClipMin), reluClipMax); // clip
      reg = _mm256_mullo_epi16(reg, reg); // square
      reg = _mm256_madd_epi16(reg, stmWeightsVec[i]); // multiply with output layer
      sum = _mm256_add_epi32(sum, reg); // collect the result,

      // Non side to move
      reg = _mm256_min_epi16(_mm256_max_epi16(oppAcc[i], reluClipMin), reluClipMax);
      reg = _mm256_mullo_epi16(reg, reg);
      reg = _mm256_madd_epi16(reg, oppWeightsVec[i]);
      sum = _mm256_add_epi32(sum, reg);
    }

#else

    for (int i = 0; i < TransformedFeatureDimensions; ++i) {

      // Side to move
      reg = std::min(std::max(stmAcc[i], reluClipMin), reluClipMax); // clip
      reg *= reg; // square
      sum += int(reg) * stmWeightsVec[i];

      // Non side to move
      reg = std::min(std::max(oppAcc[i], reluClipMin), reluClipMax);
      reg *= reg;
      sum += int(reg) * oppWeightsVec[i];
    }

#endif

    int unsquared = vecHadd(sum) / NetworkQA + Content.OutputBias;

    return Score((unsquared * NetworkScale) / NetworkQAB);
  }

}
