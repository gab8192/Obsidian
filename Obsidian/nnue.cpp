#include "nnue.h"
#include "bitboard.h"
#include "incbin.h"
#include "position.h"

#include <iostream>
#include <fstream>

INCBIN(EmbeddedNNUE, EvalFile);

#define AsVecI(x) *(VecI*)(&x)
#define AsVecF(x) *(VecF*)(&x)

namespace NNUE {

  constexpr int FloatInVec = sizeof(VecI) / sizeof(float);
  constexpr int I16InVec = sizeof(VecI) / sizeof(int16_t);
  constexpr int I8InVec = sizeof(VecI) / sizeof(int8_t);

  constexpr int FtShift = 10;

  struct {
    alignas(Alignment) int16_t FeatureWeights[KingBuckets][2][6][64][L1];
    alignas(Alignment) int16_t FeatureBiases[L1];

    alignas(Alignment) int8_t L1Weights[L1][OutputBuckets][L2];
    alignas(Alignment) float L1Biases[OutputBuckets][L2];

    alignas(Alignment) float L2Weights[L2][OutputBuckets][L3];
    alignas(Alignment) float L2Biases[OutputBuckets][L3];

    alignas(Alignment) float L3Weights[L3][OutputBuckets];
    alignas(Alignment) float L3Biases[OutputBuckets];
  } Content;

  bool needRefresh(Color side, Square oldKing, Square newKing) {
    const bool oldMirrored = fileOf(oldKing) >= FILE_E;
    const bool newMirrored = fileOf(newKing) >= FILE_E;

    if (oldMirrored != newMirrored)
      return true;

    return   KingBucketsScheme[relative_square(side, oldKing)]
          != KingBucketsScheme[relative_square(side, newKing)];
  }

  inline int16_t* featureAddress(Square kingSq, Color side, Piece pc, Square sq) {
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
    VecI* inputVec = (VecI*)input;
    VecI* outputVec = (VecI*)output;
    VecI* add0Vec = (VecI*) add0;

    for (int i = 0; i < InputSize / I16InVec; ++i)
      outputVec[i] = addEpi16(inputVec[i], add0Vec[i]);
  }

  template <int InputSize>
  inline void multiSub(weight_t* output, weight_t* input, weight_t* sub0){
    VecI* inputVec = (VecI*)input;
    VecI* outputVec = (VecI*)output;
    VecI* sub0Vec = (VecI*) sub0;

    for (int i = 0; i < InputSize / I16InVec; ++i)
      outputVec[i] = subEpi16(inputVec[i], sub0Vec[i]);
  }

  template <int InputSize>
  inline void multiAddAdd(weight_t* output, weight_t* input, weight_t* add0, weight_t* add1){
    VecI* inputVec = (VecI*)input;
    VecI* outputVec = (VecI*)output;
    VecI* add0Vec = (VecI*) add0;
    VecI* add1Vec = (VecI*) add1;

    for (int i = 0; i < InputSize / I16InVec; ++i)
      outputVec[i] = addEpi16(inputVec[i], addEpi16(add0Vec[i], add1Vec[i]));
  }

  template <int InputSize>
  inline void multiSubAdd(weight_t* output, weight_t* input, weight_t* sub0, weight_t* add0) {
    VecI* inputVec = (VecI*)input;
    VecI* outputVec = (VecI*)output;

    VecI* sub0Vec = (VecI*) sub0;
    VecI* add0Vec = (VecI*) add0;
        
    for (int i = 0; i < InputSize / I16InVec; ++i)
      outputVec[i] = subEpi16(addEpi16(inputVec[i], add0Vec[i]), sub0Vec[i]);
  }

  template <int InputSize>
  inline void multiSubAddSub(weight_t* output, weight_t* input, weight_t* sub0, weight_t* add0, weight_t* sub1) {
    VecI* inputVec = (VecI*)input;
    VecI* outputVec = (VecI*)output;

    VecI* sub0Vec = (VecI*) sub0;
    VecI* add0Vec = (VecI*) add0;
    VecI* sub1Vec = (VecI*) sub1;

    for (int i = 0; i < InputSize / I16InVec; ++i)
      outputVec[i] = subEpi16(subEpi16(addEpi16(inputVec[i], add0Vec[i]), sub0Vec[i]), sub1Vec[i]);
  }

   template <int InputSize>
  inline void multiSubAddSubAdd(weight_t* output, weight_t* input, weight_t* sub0, weight_t* add0, weight_t* sub1, weight_t* add1) {
    VecI* inputVec = (VecI*)input;
    VecI* outputVec = (VecI*)output;

    VecI* sub0Vec = (VecI*) sub0;
    VecI* add0Vec = (VecI*) add0;
    VecI* sub1Vec = (VecI*) sub1;
    VecI* add1Vec = (VecI*) add1;

    for (int i = 0; i < InputSize / I16InVec; ++i)
      outputVec[i] = addEpi16(subEpi16(subEpi16(addEpi16(inputVec[i], add0Vec[i]), sub0Vec[i]), sub1Vec[i]), add1Vec[i]);
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

    VecF vecfZero = _mm256_setzero_ps();
    VecF vecOne = _mm256_set1_ps(1.0f);
    VecI veciZero = _mm256_setzero_si256();
    VecI vecQA = _mm256_set1_epi16(NetworkQA);

    alignas(Alignment) uint8_t ftOut[L1];
    alignas(Alignment) float l1Out[L2];
    alignas(Alignment) float l2Out[L3];
    float l3Out;

    { // activate FT
      for (int them = 0; them <= 1; ++them) 
        {
          int16_t* acc = accumulator.colors[pos.sideToMove ^ them];
          for (int i = 0; i < L1 / 2; i += I8InVec) 
          {
            VecI c0 = minEpi16(maxEpi16(AsVecI(acc[i]), veciZero), vecQA);
            VecI c1 = minEpi16(maxEpi16(AsVecI(acc[i + L1/2]), veciZero), vecQA);

            VecI d0 = minEpi16(maxEpi16(AsVecI(acc[i + 16]), veciZero), vecQA);
            VecI d1 = minEpi16(maxEpi16(AsVecI(acc[i + L1/2 + 16]), veciZero), vecQA);

            // FtShift ensures the values are on a range 0 <-> 31
            VecI cProd = _mm256_srli_epi16(_mm256_mullo_epi16(c0, c1), FtShift);
            VecI dProd = _mm256_srli_epi16(_mm256_mullo_epi16(d0, d1), FtShift);

            VecI packed = _mm256_packus_epi16(cProd, dProd);
            // packus does not concatenate the two vectors. it takes half
            // of one, and half of the other, twice, so we must sort it back
            AsVecI(ftOut[them * L1 / 2 + i]) = _mm256_permute4x64_epi64(packed, _MM_SHUFFLE(3, 1, 2, 0));
          }
        }
    }
    
    { // propagate l1
      int32_t sums[L2];
      memset(sums, 0, sizeof(sums));

      for (int i = 0; i < L1; ++i) {
        VecI vecFtOut = _mm256_set1_epi16(ftOut[i]);
        for (int j = 0; j < L2; j += I16InVec) {
          VecI vecWeight = AsVecI(Content.L1Weights[i][bucket][j]);
          AsVecI(sums[j]) = _mm256_add_epi16(AsVecI(sums[j]), mulloEpi16(vecFtOut, vecWeight));
        }
      }

      float kek[L2];
      for (int i = 0; i < L2; i++)
        kek[i] = Content.L1Biases[bucket][i] + float(sums[i]) / 32258.0f;

      for (int i = 0; i < L2; i += FloatInVec)
        AsVecF(l1Out[i]) = _mm256_min_ps(_mm256_max_ps(AsVecF(kek[i]), vecfZero), vecOne);
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
