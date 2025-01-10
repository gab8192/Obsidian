#include "nnue.h"
#include "bitboard.h"
#include "incbin.h"
#include "position.h"

#include <iostream>
#include <fstream>

INCBIN(EmbeddedNNUE, EvalFile);

#define AsVec(x) *(Vec*)(&x)

namespace NNUE {

  constexpr int WeightsPerVec = sizeof(Vec) / sizeof(weight_t);

  struct {
    alignas(Alignment) weight_t FeatureWeights[KingBuckets][2][6][64][L1];
    alignas(Alignment) weight_t FeatureBiases[L1];

    alignas(Alignment) weight_t L1Weights[L1][OutputBuckets][L2];
    alignas(Alignment) weight_t L1Biases[OutputBuckets][L2];

    alignas(Alignment) weight_t L2Weights[L2][OutputBuckets][L3];
    alignas(Alignment) weight_t L2Biases[OutputBuckets][L3];

    alignas(Alignment) weight_t L3Weights[L3][OutputBuckets];
    alignas(Alignment) weight_t L3Biases[OutputBuckets];
  } Content;

  bool needRefresh(Color side, Square oldKing, Square newKing) {
    const bool oldMirrored = fileOf(oldKing) >= FILE_E;
    const bool newMirrored = fileOf(newKing) >= FILE_E;

    if (oldMirrored != newMirrored)
      return true;

    return   KingBucketsScheme[relative_square(side, oldKing)]
          != KingBucketsScheme[relative_square(side, newKing)];
  }

  inline weight_t* featureAddress(Square kingSq, Color side, Piece pc, Square sq) {
    if (fileOf(kingSq) >= FILE_E)
      sq = Square(sq ^ 7);

    return Content.FeatureWeights
            [KingBucketsScheme[relative_square(side, kingSq)]]
            [side != piece_color(pc)]
            [piece_type(pc)-1]
            [relative_square(side, sq)];
  }

  template <int InputSize>
  inline void multiAdd(weight_t* output, weight_t* input, weight_t* add0){
    Vec* inputVec = (Vec*)input;
    Vec* outputVec = (Vec*)output;
    Vec* add0Vec = (Vec*) add0;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      outputVec[i] = _mm256_add_ps(inputVec[i], add0Vec[i]);
  }

  template <int InputSize>
  inline void multiSub(weight_t* output, weight_t* input, weight_t* sub0){
    Vec* inputVec = (Vec*)input;
    Vec* outputVec = (Vec*)output;
    Vec* sub0Vec = (Vec*) sub0;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      outputVec[i] = _mm256_sub_ps(inputVec[i], sub0Vec[i]);
  }

  template <int InputSize>
  inline void multiAddAdd(weight_t* output, weight_t* input, weight_t* add0, weight_t* add1){
    Vec* inputVec = (Vec*)input;
    Vec* outputVec = (Vec*)output;
    Vec* add0Vec = (Vec*) add0;
    Vec* add1Vec = (Vec*) add1;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      outputVec[i] = _mm256_add_ps(inputVec[i], _mm256_add_ps(add0Vec[i], add1Vec[i]));
  }

  template <int InputSize>
  inline void multiSubAdd(weight_t* output, weight_t* input, weight_t* sub0, weight_t* add0) {
    Vec* inputVec = (Vec*)input;
    Vec* outputVec = (Vec*)output;

    Vec* sub0Vec = (Vec*) sub0;
    Vec* add0Vec = (Vec*) add0;
        
    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      outputVec[i] = _mm256_sub_ps(_mm256_add_ps(inputVec[i], add0Vec[i]), sub0Vec[i]);
  }

  template <int InputSize>
  inline void multiSubAddSub(weight_t* output, weight_t* input, weight_t* sub0, weight_t* add0, weight_t* sub1) {
    Vec* inputVec = (Vec*)input;
    Vec* outputVec = (Vec*)output;

    Vec* sub0Vec = (Vec*) sub0;
    Vec* add0Vec = (Vec*) add0;
    Vec* sub1Vec = (Vec*) sub1;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      outputVec[i] = _mm256_sub_ps(_mm256_sub_ps(_mm256_add_ps(inputVec[i], add0Vec[i]), sub0Vec[i]), sub1Vec[i]);
  }

   template <int InputSize>
  inline void multiSubAddSubAdd(weight_t* output, weight_t* input, weight_t* sub0, weight_t* add0, weight_t* sub1, weight_t* add1) {
    Vec* inputVec = (Vec*)input;
    Vec* outputVec = (Vec*)output;

    Vec* sub0Vec = (Vec*) sub0;
    Vec* add0Vec = (Vec*) add0;
    Vec* sub1Vec = (Vec*) sub1;
    Vec* add1Vec = (Vec*) add1;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      outputVec[i] = _mm256_add_ps(_mm256_sub_ps(_mm256_sub_ps(_mm256_add_ps(inputVec[i], add0Vec[i]), sub0Vec[i]), sub1Vec[i]), add1Vec[i]);
  }

  void Accumulator::addPiece(Square kingSq, Color side, Piece pc, Square sq) {
    multiAdd<L1>(colors[side], colors[side], featureAddress(kingSq, side, pc, sq));
  }

  void Accumulator::removePiece(Square kingSq, Color side, Piece pc, Square sq) {
    multiSub<L1>(colors[side], colors[side], featureAddress(kingSq, side, pc, sq));
  }

  void Accumulator::doUpdates(Square kingSq, Color side, Accumulator& input) {
    DirtyPieces dp = this->dirtyPieces;
    if (dp.type == DirtyPieces::CASTLING) 
    {
      multiSubAddSubAdd<L1>(colors[side], input.colors[side], 
        featureAddress(kingSq, side, dp.sub0.pc, dp.sub0.sq),
        featureAddress(kingSq, side, dp.add0.pc, dp.add0.sq),
        featureAddress(kingSq, side, dp.sub1.pc, dp.sub1.sq),
        featureAddress(kingSq, side, dp.add1.pc, dp.add1.sq));
    } else if (dp.type == DirtyPieces::CAPTURE) 
    { 
      multiSubAddSub<L1>(colors[side], input.colors[side], 
        featureAddress(kingSq, side, dp.sub0.pc, dp.sub0.sq),
        featureAddress(kingSq, side, dp.add0.pc, dp.add0.sq),
        featureAddress(kingSq, side, dp.sub1.pc, dp.sub1.sq));
    } else
    {
      multiSubAdd<L1>(colors[side], input.colors[side], 
        featureAddress(kingSq, side, dp.sub0.pc, dp.sub0.sq),
        featureAddress(kingSq, side, dp.add0.pc, dp.add0.sq));
    }
    updated[side] = true;
  }

  void Accumulator::reset(Color side) {
    memcpy(colors[side], Content.FeatureBiases, sizeof(Content.FeatureBiases));
  }

  void Accumulator::refresh(Position& pos, Color side) {
    reset(side);
    const Square kingSq = pos.kingSquare(side);
    Bitboard occupied = pos.pieces();
    while (occupied) {
      const Square sq = popLsb(occupied);
      addPiece(kingSq, side, pos.board[sq], sq);
    }
    updated[side] = true;
  }

  void FinnyEntry::reset() {
    memset(byColorBB, 0, sizeof(byColorBB));
    memset(byPieceBB, 0, sizeof(byPieceBB));
    acc.reset(WHITE);
    acc.reset(BLACK);
  }

  void init() {

    memcpy(&Content, gEmbeddedNNUEData, sizeof(Content));
    
  }

  float vecHadd(__m256 x) {
    float sum = 0;
    float lol[8];
    _mm256_storeu_ps(lol, x);

    for (int i = 0; i < 8; i++)
      sum += lol[i];

    return sum;
  }

  Score evaluate(Position& pos, Accumulator& accumulator) {

    constexpr int divisor = (32 + OutputBuckets - 1) / OutputBuckets;
    int bucket = (BitCount(pos.pieces()) - 2) / divisor;

    Vec vecZero = _mm256_setzero_ps();
    Vec vecOne = _mm256_set1_ps(1.0f);

    alignas(Alignment) float ftOut[L1];
    alignas(Alignment) float l1Out[L2];
    alignas(Alignment) float l2Out[L3];
    float l3Out;

    { // activate FT
      for (int them = 0; them <= 1; ++them) 
        {
          float* acc = accumulator.colors[pos.sideToMove ^ them];
          for (int i = 0; i < L1 / 2; i += WeightsPerVec) 
          {
            Vec c0 = _mm256_min_ps(_mm256_max_ps(AsVec(acc[i]), vecZero), vecOne);
            Vec c1 = _mm256_min_ps(_mm256_max_ps(AsVec(acc[i + L1/2]), vecZero), vecOne);

            AsVec(ftOut[them * L1 / 2 + i]) = _mm256_mul_ps(c0, c1);
          }
        }
    }
    
    { // propagate l1
      float sums[L2];
      for (int i = 0; i < L2; i++)
        sums[i] = Content.L1Biases[bucket][i];

      for (int i = 0; i < L1; ++i) {
        Vec vecFtOut = _mm256_set1_ps(ftOut[i]);
        for (int j = 0; j < L2; j += WeightsPerVec) {
          Vec vecWeight = AsVec(Content.L1Weights[i][bucket][j]);
          AsVec(sums[j]) = _mm256_add_ps(AsVec(sums[j]), _mm256_mul_ps(vecFtOut, vecWeight));
        }
      }

      for (int i = 0; i < L2; i += WeightsPerVec)
        AsVec(l1Out[i]) = _mm256_min_ps(_mm256_max_ps(AsVec(sums[i]), vecZero), vecOne);
    }

    { // propagate l2
      float sums[L3];
      for (int i = 0; i < L3; i++)
        sums[i] = Content.L2Biases[bucket][i];

      for (int i = 0; i < L2; ++i) {
        for (int j = 0; j < L3; ++j)
          sums[j] += l1Out[i] * Content.L2Weights[i][bucket][j];
      }

      for (int i = 0; i < L3; ++i)
        l2Out[i] = std::clamp(sums[i], 0.0f, 1.0f);
    }

    { // propagate l3
      float sums = Content.L3Biases[bucket];
      for (int i = 0; i < L3; ++i)
        sums += l2Out[i] * Content.L3Weights[i][bucket];

      l3Out = sums;
    }


    return l3Out * NetworkScale;
  }

}
