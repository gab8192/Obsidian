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

  constexpr int WeightsPerVec = sizeof(SIMD::Vec) / sizeof(weight_t);

  alignas(SIMD::Alignment) int FeatureIndexTable[COLOR_NB][PIECE_NB][SQUARE_NB];

  struct {
    alignas(SIMD::Alignment) weight_t FeatureWeights[FeatureDimensions * TransformedFeatureDimensions];
    alignas(SIMD::Alignment) weight_t FeatureBiases[TransformedFeatureDimensions];
    alignas(SIMD::Alignment) weight_t OutputWeights[2 * TransformedFeatureDimensions];
                             weight_t OutputBias;
  } Content;

  template <int InputSize>
  inline void addToAll(weight_t* input, int offset)
  {
    offset /= WeightsPerVec;

    Vec* inputVec = (Vec*)input;
    Vec* weightsVec = (Vec*)Content.FeatureWeights;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i) {
      inputVec[i] = addEpi16(inputVec[i], weightsVec[offset + i]);
    }
  }

  template <int InputSize>
  inline void subtractFromAll(weight_t* input, int offset)
  {
    offset /= WeightsPerVec;

    Vec* inputVec = (Vec*)input;
    Vec* weightsVec = (Vec*)Content.FeatureWeights;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i) {
      inputVec[i] = subEpi16(inputVec[i], weightsVec[offset + i]);
    }
  }

  template <int InputSize>
  inline void addAndSubtractFromAll(weight_t* input, int addOff, int subtractOff) {
    addOff /= WeightsPerVec;
    subtractOff /= WeightsPerVec;

    Vec* inputVec = (Vec*)input;
    Vec* weightsVec = (Vec*)Content.FeatureWeights;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i) {
      inputVec[i] = subEpi16(addEpi16(inputVec[i], weightsVec[addOff + i]), weightsVec[subtractOff + i]);
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

  inline int16_t SCRelu(weight_t x) {
    int16_t clipped = std::clamp<int16_t>(x, 0, 181);
    return clipped * clipped;
  }

  Score evaluate(Accumulator& accumulator, Color sideToMove) {

    Vec* stmAcc;
    Vec* oppAcc;

    if (sideToMove == WHITE) {
      stmAcc = (Vec*) accumulator.white;
      oppAcc = (Vec*) accumulator.black;
    }
    else {
      stmAcc = (Vec*) accumulator.black;
      oppAcc = (Vec*) accumulator.white;
    }

    Vec* stmWeightsVec = (Vec*) &Content.OutputWeights[0];
    Vec* oppWeightsVec = (Vec*) &Content.OutputWeights[TransformedFeatureDimensions];

    const Vec reluClipMin = _mm256_setzero_si256();
    const Vec reluClipMax = _mm256_set1_epi16(181);

    Vec sumVec = _mm256_setzero_si256();
    Vec reg;

    for (int i = 0; i < TransformedFeatureDimensions / WeightsPerVec; ++i) {

      // Side to move
      reg = _mm256_min_epi16(_mm256_max_epi16(stmAcc[i], reluClipMin), reluClipMax);
      reg = _mm256_mullo_epi16(reg, reg);
      reg = _mm256_madd_epi16(reg, stmWeightsVec[i]);
      sumVec = _mm256_add_epi32(sumVec, reg);

      // Non side to move
      reg = _mm256_min_epi16(_mm256_max_epi16(oppAcc[i], reluClipMin), reluClipMax);
      reg = _mm256_mullo_epi16(reg, reg);
      reg = _mm256_madd_epi16(reg, oppWeightsVec[i]);
      sumVec = _mm256_add_epi32(sumVec, reg);
    }

    int sum = vecHadd(sumVec);

    int unsquared = sum / 255 + Content.OutputBias;

    return Score((unsquared * NetworkScale) / NetworkQ);
  }

}
