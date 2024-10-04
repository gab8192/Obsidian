#include "nnue.h"
#include "bitboard.h"
#include "incbin.h"
#include "obsnuma.h"
#include "position.h"

#include <iostream>
#include <fstream>

INCBIN(EmbeddedNNUE, EvalFile);

namespace NNUE {

  constexpr int WeightsPerVec = sizeof(Vec) / sizeof(weight_t);

  NNWeights** weightsPool;

  bool needRefresh(Color side, Square oldKing, Square newKing) {
    // Crossed half?
    if ((oldKing & 0b100) != (newKing & 0b100))
      return true;

    return   KingBucketsScheme[relative_square(side, oldKing)]
          != KingBucketsScheme[relative_square(side, newKing)];
  }

  inline weight_t* featureAddress(Square kingSq, Color side, Piece pc, Square sq, NNWeights& nWeights) {
    if (kingSq & 0b100)
      sq = Square(sq ^ 7);

    return nWeights.FeatureWeights
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
      outputVec[i] = addEpi16(inputVec[i], add0Vec[i]);
  }

  template <int InputSize>
  inline void multiSub(weight_t* output, weight_t* input, weight_t* sub0){
    Vec* inputVec = (Vec*)input;
    Vec* outputVec = (Vec*)output;
    Vec* sub0Vec = (Vec*) sub0;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      outputVec[i] = subEpi16(inputVec[i], sub0Vec[i]);
  }

  template <int InputSize>
  inline void multiAddAdd(weight_t* output, weight_t* input, weight_t* add0, weight_t* add1){
    Vec* inputVec = (Vec*)input;
    Vec* outputVec = (Vec*)output;
    Vec* add0Vec = (Vec*) add0;
    Vec* add1Vec = (Vec*) add1;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      outputVec[i] = addEpi16(inputVec[i], addEpi16(add0Vec[i], add1Vec[i]));
  }

  template <int InputSize>
  inline void multiSubAdd(weight_t* output, weight_t* input, weight_t* sub0, weight_t* add0) {
    Vec* inputVec = (Vec*)input;
    Vec* outputVec = (Vec*)output;

    Vec* sub0Vec = (Vec*) sub0;
    Vec* add0Vec = (Vec*) add0;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      outputVec[i] = subEpi16(addEpi16(inputVec[i], add0Vec[i]), sub0Vec[i]);
  }

  template <int InputSize>
  inline void multiSubAddSub(weight_t* output, weight_t* input, weight_t* sub0, weight_t* add0, weight_t* sub1) {
    Vec* inputVec = (Vec*)input;
    Vec* outputVec = (Vec*)output;

    Vec* sub0Vec = (Vec*) sub0;
    Vec* add0Vec = (Vec*) add0;
    Vec* sub1Vec = (Vec*) sub1;

    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      outputVec[i] = subEpi16(subEpi16(addEpi16(inputVec[i], add0Vec[i]), sub0Vec[i]), sub1Vec[i]);
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
      outputVec[i] = addEpi16(subEpi16(subEpi16(addEpi16(inputVec[i], add0Vec[i]), sub0Vec[i]), sub1Vec[i]), add1Vec[i]);
  }

  void Accumulator::addPiece(Square kingSq, Color side, Piece pc, Square sq, NNWeights& nWeights) {
    multiAdd<HiddenWidth>(colors[side], colors[side], featureAddress(kingSq, side, pc, sq, nWeights));
  }

  void Accumulator::removePiece(Square kingSq, Color side, Piece pc, Square sq, NNWeights& nWeights) {
    multiSub<HiddenWidth>(colors[side], colors[side], featureAddress(kingSq, side, pc, sq, nWeights));
  }

  void Accumulator::doUpdates(Square kingSq, Color side, Accumulator& input, NNWeights& nWeights) {
    DirtyPieces dp = this->dirtyPieces;
    if (dp.type == DirtyPieces::CASTLING)
    {
      multiSubAddSubAdd<HiddenWidth>(colors[side], input.colors[side],
        featureAddress(kingSq, side, dp.sub0.pc, dp.sub0.sq, nWeights),
        featureAddress(kingSq, side, dp.add0.pc, dp.add0.sq, nWeights),
        featureAddress(kingSq, side, dp.sub1.pc, dp.sub1.sq, nWeights),
        featureAddress(kingSq, side, dp.add1.pc, dp.add1.sq, nWeights));
    } else if (dp.type == DirtyPieces::CAPTURE)
    {
      multiSubAddSub<HiddenWidth>(colors[side], input.colors[side],
        featureAddress(kingSq, side, dp.sub0.pc, dp.sub0.sq, nWeights),
        featureAddress(kingSq, side, dp.add0.pc, dp.add0.sq, nWeights),
        featureAddress(kingSq, side, dp.sub1.pc, dp.sub1.sq, nWeights));
    } else
    {
      multiSubAdd<HiddenWidth>(colors[side], input.colors[side],
        featureAddress(kingSq, side, dp.sub0.pc, dp.sub0.sq, nWeights),
        featureAddress(kingSq, side, dp.add0.pc, dp.add0.sq, nWeights));
    }
    updated[side] = true;
  }

  void Accumulator::reset(Color side, NNWeights& nWeights) {
    memcpy(colors[side], nWeights.FeatureBiases, sizeof(nWeights.FeatureBiases));
  }

  void Accumulator::refresh(Position& pos, Color side, NNWeights& nWeights) {
    reset(side, nWeights);
    const Square kingSq = pos.kingSquare(side);
    Bitboard occupied = pos.pieces();
    while (occupied) {
      const Square sq = popLsb(occupied);
      addPiece(kingSq, side, pos.board[sq], sq, nWeights);
    }
    updated[side] = true;
  }

  void FinnyEntry::reset(NNWeights& nWeights) {
    memset(byColorBB, 0, sizeof(byColorBB));
    memset(byPieceBB, 0, sizeof(byPieceBB));
    acc.reset(WHITE, nWeights);
    acc.reset(BLACK, nWeights);
  }


  void* aligned_numa_alloc(size_t align, size_t size, int node) {
    void *ptr = numa_alloc_onnode(size + align - 1, node);
    uintptr_t aligned_ptr = ((uintptr_t)ptr + align - 1) & ~(align - 1);
    return (void *)aligned_ptr;
  }

  void init() {

    weightsPool = new NNWeights*[numaNodeCount()];
    for (int node = 0; node < numaNodeCount(); node++) {
      NNWeights* thisWeights = (NNWeights*) aligned_numa_alloc(SIMD::Alignment, sizeof(NNWeights), node);
      memcpy(thisWeights, gEmbeddedNNUEData, sizeof(NNWeights));
      weightsPool[node] = thisWeights;
    }
  }

  Score evaluate(Position& pos, Accumulator& accumulator, NNWeights& nWeights) {

    constexpr int divisor = (32 + OutputBuckets - 1) / OutputBuckets;
    int outputBucket = (BitCount(pos.pieces()) - 2) / divisor;

    Vec vecZero = vecSetZero();
    Vec vecQA = vecSet1Epi16(NetworkQA);

    Vec sum = vecZero;

    for (int them = 0; them <= 1; ++them)
    {
      Vec* acc = (Vec*) accumulator.colors[pos.sideToMove ^ them];
      Vec* weights = (Vec*) &nWeights.OutputWeights[outputBucket][them * HiddenWidth / 2];
      for (int i = 0; i < (HiddenWidth / WeightsPerVec) / 2; ++i)
      {
        Vec c0 = minEpi16(maxEpi16(acc[i], vecZero), vecQA);
        Vec c1 = minEpi16(maxEpi16(acc[i + (HiddenWidth / WeightsPerVec) / 2], vecZero), vecQA);
        Vec prod = maddEpi16(mulloEpi16(c0, weights[i]), c1);
        sum = addEpi32(sum, prod);
      }
    }

    int unsquared = vecHaddEpi32(sum) / NetworkQA + nWeights.OutputBias[outputBucket];

    return (unsquared * NetworkScale) / NetworkQAB;
  }

}
