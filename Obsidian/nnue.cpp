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

  struct {
    alignas(SIMD::Alignment) weight_t FeatureWeights[FeatureDimensions * KingBucketsNB * TransformedFeatureDimensions];
    alignas(SIMD::Alignment) weight_t FeatureBiases[TransformedFeatureDimensions];
    alignas(SIMD::Alignment) weight_t OutputWeights[2 * TransformedFeatureDimensions];
                             weight_t OutputBias;
  } Content;

  int featureIndex(Color stm, Piece pc, Square sq, Square king) {

    PieceType pcType = ptypeOf(pc);
    Color pcColor = colorOf(pc);

    int result;
    if (pcColor == stm)
      result = SQUARE_NB * (pcType - 1) + relative_square(stm, sq);
    else
      result = SQUARE_NB * (pcType + 5) + relative_square(stm, sq);

    result += FeatureDimensions * KingBuckets[relative_square(stm, king)];

    return result * TransformedFeatureDimensions;
  }

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

  void Accumulator::activateFeature(Square sq, Piece pc, Square wKing, Square bKing) {
    addToAll<TransformedFeatureDimensions>(white, featureIndex(WHITE, pc, sq, wKing));
    addToAll<TransformedFeatureDimensions>(black, featureIndex(BLACK, pc, sq, bKing));
  }

  void Accumulator::deactivateFeature(Square sq, Piece pc, Square wKing, Square bKing) {
    subtractFromAll<TransformedFeatureDimensions>(white, featureIndex(WHITE, pc, sq, wKing));
    subtractFromAll<TransformedFeatureDimensions>(black, featureIndex(BLACK, pc, sq, bKing));
  }

  void Accumulator::moveFeature(Square from, Square to, Piece pc, Square wKing, Square bKing) {
    addAndSubtractFromAll<TransformedFeatureDimensions>(white,
      featureIndex(WHITE, pc, to, wKing), featureIndex(WHITE, pc, from, wKing));
    addAndSubtractFromAll<TransformedFeatureDimensions>(black,
      featureIndex(BLACK, pc, to, bKing), featureIndex(BLACK, pc, from, bKing));
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
  }

  inline int SCRelu(weight_t x) {
    int16_t clipped = std::clamp<int16_t>(x, 0, 255);
    int wide = clipped;
    return wide * wide;
  }

  Score evaluate(Accumulator& accumulator, Color sideToMove) {

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

    int sum = 0;

    for (int i = 0; i < TransformedFeatureDimensions; ++i) {
      sum += SCRelu(stmAccumulator[i]) * Content.OutputWeights[i];
      sum += SCRelu(oppAccumulator[i]) * Content.OutputWeights[TransformedFeatureDimensions + i];
    }

    int unsquared = sum / 255 + Content.OutputBias;

    return Score((unsquared * NetworkScale) / NetworkQ);
  }

}
