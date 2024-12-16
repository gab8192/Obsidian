#include "nnue.h"
#include "bitboard.h"
#include "incbin.h"
#include "position.h"
#include "util.h"

#include <iostream>
#include <fstream>

INCBIN(EmbeddedNNUE, EvalFile);

namespace NNUE {

  constexpr int WeightsPerVec = sizeof(Vec) / sizeof(weight_t);

  struct Net {
    alignas(Alignment) weight_t FeatureWeights[KingBuckets][2][6][64][HiddenWidth];
    alignas(Alignment) weight_t FeatureBiases[HiddenWidth];
    alignas(Alignment) weight_t OutputWeights[OutputBuckets][HiddenWidth];
                       weight_t OutputBias[OutputBuckets];
  };

  Net* Content;

  bool needRefresh(Color side, Square oldKing, Square newKing) {
    // Crossed half?
    if ((oldKing & 0b100) != (newKing & 0b100))
      return true;

    return   KingBucketsScheme[relative_square(side, oldKing)]
          != KingBucketsScheme[relative_square(side, newKing)];
  }

  inline Vec* featureAddress(Square kingSq, Color side, Piece pc, Square sq) {
    if (kingSq & 0b100)
      sq = Square(sq ^ 7);

    return (Vec*) Content->FeatureWeights
            [KingBucketsScheme[relative_square(side, kingSq)]]
            [side != piece_color(pc)]
            [piece_type(pc)-1]
            [relative_square(side, sq)];
  }

  template <int InputSize>
  inline void multiAdd(Vec* output, Vec* input, Vec* add0){
    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      output[i] = addEpi16(input[i], add0[i]);
  }

  template <int InputSize>
  inline void multiSub(Vec* output, Vec* input, Vec* sub0){
    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      output[i] = subEpi16(input[i], sub0[i]);
  }

  template <int InputSize>
  inline void multiAddAdd(Vec* output, Vec* input, Vec* add0, Vec* add1){
    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      output[i] = addEpi16(input[i], addEpi16(add0[i], add1[i]));
  }

  template <int InputSize>
  inline void multiSubAdd(Vec* output, Vec* input, Vec* sub0, Vec* add0) {
    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      output[i] = subEpi16(addEpi16(input[i], add0[i]), sub0[i]);
  }

  template <int InputSize>
  inline void multiSubAddSub(Vec* output, Vec* input, Vec* sub0, Vec* add0, Vec* sub1) {
    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      output[i] = subEpi16(subEpi16(addEpi16(input[i], add0[i]), sub0[i]), sub1[i]);
  }

   template <int InputSize>
  inline void multiSubAddSubAdd(Vec* output, Vec* input, Vec* sub0, Vec* add0, Vec* sub1, Vec* add1) {
    for (int i = 0; i < InputSize / WeightsPerVec; ++i)
      output[i] = addEpi16(subEpi16(subEpi16(addEpi16(input[i], add0[i]), sub0[i]), sub1[i]), add1[i]);
  }

  void Accumulator::addPiece(Square kingSq, Color side, Piece pc, Square sq) {
    multiAdd<HiddenWidth>((Vec*) colors[side], (Vec*) colors[side], featureAddress(kingSq, side, pc, sq));
  }

  void Accumulator::removePiece(Square kingSq, Color side, Piece pc, Square sq) {
    multiSub<HiddenWidth>((Vec*) colors[side], (Vec*) colors[side], featureAddress(kingSq, side, pc, sq));
  }

  void Accumulator::doUpdates(Square kingSq, Color side, Accumulator& input) {
    DirtyPieces dp = this->dirtyPieces;
    if (dp.type == DirtyPieces::CASTLING)
    {
      multiSubAddSubAdd<HiddenWidth>((Vec*) colors[side], (Vec*) input.colors[side],
        featureAddress(kingSq, side, dp.sub0.pc, dp.sub0.sq),
        featureAddress(kingSq, side, dp.add0.pc, dp.add0.sq),
        featureAddress(kingSq, side, dp.sub1.pc, dp.sub1.sq),
        featureAddress(kingSq, side, dp.add1.pc, dp.add1.sq));
    } else if (dp.type == DirtyPieces::CAPTURE)
    {
      multiSubAddSub<HiddenWidth>((Vec*) colors[side], (Vec*) input.colors[side],
        featureAddress(kingSq, side, dp.sub0.pc, dp.sub0.sq),
        featureAddress(kingSq, side, dp.add0.pc, dp.add0.sq),
        featureAddress(kingSq, side, dp.sub1.pc, dp.sub1.sq));
    } else
    {
      multiSubAdd<HiddenWidth>((Vec*) colors[side], (Vec*) input.colors[side],
        featureAddress(kingSq, side, dp.sub0.pc, dp.sub0.sq),
        featureAddress(kingSq, side, dp.add0.pc, dp.add0.sq));
    }
    updated[side] = true;
  }

  void Accumulator::reset(Color side) {
    memcpy(colors[side], Content->FeatureBiases, sizeof(Content->FeatureBiases));
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
    Content = (Net*) gEmbeddedNNUEData;
  }

  Score evaluate(Position& pos, Accumulator& accumulator) {

    constexpr int divisor = (32 + OutputBuckets - 1) / OutputBuckets;
    int outputBucket = (BitCount(pos.pieces()) - 2) / divisor;

    Vec vecZero = vecSetZero();
    Vec vecQA = vecSet1Epi16(NetworkQA);

    Vec sum = vecZero;

    for (int them = 0; them <= 1; ++them)
    {
      Vec* acc = (Vec*) accumulator.colors[pos.sideToMove ^ them];
      Vec* weights = (Vec*) &Content->OutputWeights[outputBucket][them * HiddenWidth / 2];
      for (int i = 0; i < (HiddenWidth / WeightsPerVec) / 2; ++i)
      {
        Vec c0 = minEpi16(maxEpi16(acc[i], vecZero), vecQA);
        Vec c1 = minEpi16(maxEpi16(acc[i + (HiddenWidth / WeightsPerVec) / 2], vecZero), vecQA);
        Vec prod = maddEpi16(mulloEpi16(c0, weights[i]), c1);
        sum = addEpi32(sum, prod);
      }
    }

    int unsquared = vecHaddEpi32(sum) / NetworkQA + Content->OutputBias[outputBucket];

    return (unsquared * NetworkScale) / NetworkQAB;
  }

}
