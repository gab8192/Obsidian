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
      outputVec[i] = _mm256_add_ps(inputVec[i], weightsVec[offset + i]);
  }

  template <int InputSize>
  inline void subAll(weight_t* output, weight_t* input, int offset)
  {
    offset /= WeightsPerVec;

    Vec* inputVec = (Vec*)input;
    Vec* outputVec = (Vec*)output;
    Vec* weightsVec = (Vec*)Content.FeatureWeights;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      outputVec[i] = _mm256_sub_ps(inputVec[i], weightsVec[offset + i]);
  }

  template <int InputSize>
  inline void addSubAll(weight_t* output, weight_t* input, int addOff, int subtractOff) {
    addOff /= WeightsPerVec;
    subtractOff /= WeightsPerVec;

    Vec* inputVec = (Vec*)input;
    Vec* outputVec = (Vec*)output;
    Vec* weightsVec = (Vec*)Content.FeatureWeights;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      outputVec[i] = _mm256_sub_ps(_mm256_add_ps(inputVec[i], weightsVec[addOff + i]), weightsVec[subtractOff + i]);
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

    Vec* stmWeights = (Vec*) &Content.OutputWeights[0];
    Vec* oppWeights = (Vec*) &Content.OutputWeights[HiddenWidth];

    const Vec reluClipMin = _mm256_setzero_ps();
    const Vec reluClipMax = _mm256_set1_ps(1.0);

    Vec sum = _mm256_setzero_ps();
    Vec reg;

    for (int i = 0; i < HiddenWidth / WeightsPerVec; ++i) {
      // Side to move
      reg = _mm256_max_ps(stmAcc[i], reluClipMin); // clip
      reg = _mm256_min_ps(reg, reluClipMax); // clip
      reg = _mm256_mul_ps(reg, reg); // square
      reg = _mm256_mul_ps(reg, stmWeights[i]); // multiply with output weight
      sum = _mm256_add_ps(sum, reg); // collect the result

      // Non side to move
      reg = _mm256_max_ps(oppAcc[i], reluClipMin);
      reg = _mm256_min_ps(reg, reluClipMax);
      reg = _mm256_mul_ps(reg, reg);
      reg = _mm256_mul_ps(reg, oppWeights[i]);
      sum = _mm256_add_ps(sum, reg);
    }

    float result = vecHaddPs(sum) + Content.OutputBias;

    return Score(result * 400.0f);
  }

}
