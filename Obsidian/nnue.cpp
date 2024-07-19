#include "nnue.h"
#include "bitboard.h"
#include "incbin.h"
#include "position.h"

#include <iostream>
#include <fstream>

INCBIN(EmbeddedNNUE, EvalFile);

namespace NNUE {

  constexpr int WeightsPerVec = sizeof(Vec) / sizeof(weight_t);

  struct {
    alignas(Alignment) weight_t FeatureWeights[KingBuckets][2][6][64][HiddenWidth];
    alignas(Alignment) weight_t FeatureBiases[HiddenWidth];
    alignas(Alignment) weight_t OutputWeights[OutputBuckets][2 * HiddenWidth];
                       weight_t OutputBias[OutputBuckets];
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

  void Accumulator::addPiece(Square kingSq, Color side, Piece pc, Square sq) {
    multiAdd<HiddenWidth>(colors[side], colors[side], featureAddress(kingSq, side, pc, sq));
  }

  void Accumulator::removePiece(Square kingSq, Color side, Piece pc, Square sq) {
    multiSub<HiddenWidth>(colors[side], colors[side], featureAddress(kingSq, side, pc, sq));
  }

  void Accumulator::doUpdates(Square kingSq, Color side, Accumulator& input) {
    DirtyPieces dp = this->dirtyPieces;
    if (dp.type == DirtyPieces::CASTLING) 
    {
      multiSubAddSubAdd<HiddenWidth>(colors[side], input.colors[side], 
        featureAddress(kingSq, side, dp.sub0.pc, dp.sub0.sq),
        featureAddress(kingSq, side, dp.add0.pc, dp.add0.sq),
        featureAddress(kingSq, side, dp.sub1.pc, dp.sub1.sq),
        featureAddress(kingSq, side, dp.add1.pc, dp.add1.sq));
    } else if (dp.type == DirtyPieces::CAPTURE) 
    { 
      multiSubAddSub<HiddenWidth>(colors[side], input.colors[side], 
        featureAddress(kingSq, side, dp.sub0.pc, dp.sub0.sq),
        featureAddress(kingSq, side, dp.add0.pc, dp.add0.sq),
        featureAddress(kingSq, side, dp.sub1.pc, dp.sub1.sq));
    } else
    {
      multiSubAdd<HiddenWidth>(colors[side], input.colors[side], 
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

  Score evaluate(Position& pos, Accumulator& accumulator) {

    constexpr int divisor = (32 + OutputBuckets - 1) / OutputBuckets;
    int outputBucket = (BitCount(pos.pieces()) - 2) / divisor;

    Vec* stmAcc = (Vec*) accumulator.colors[pos.sideToMove];
    Vec* oppAcc = (Vec*) accumulator.colors[~pos.sideToMove];

    Vec* stmWeights = (Vec*) &Content.OutputWeights[outputBucket][0];
    Vec* oppWeights = (Vec*) &Content.OutputWeights[outputBucket][HiddenWidth];

    const Vec vecZero = vecSetZero();
    const Vec vecQA = vecSet1Epi16(NetworkQA);

    Vec sum = vecSetZero();
    Vec s0, s1, n0, n1;

    for (int i = 0; i < HiddenWidth / WeightsPerVec; ++i) {
      s0 = maxEpi16(stmAcc[i], vecZero); // clip
      n0 = maxEpi16(oppAcc[i], vecZero); // clip

      s0 = minEpi16(s0, vecQA); // clip
      n0 = minEpi16(n0, vecQA); // clip

      s1 = mulloEpi16(s0, stmWeights[i]); // square
      n1 = mulloEpi16(n0, oppWeights[i]);

      s1 = maddEpi16(s1, s0); // multiply with output layer
      n1 = maddEpi16(n1, n0);

      sum = addEpi32(sum, s1); // collect the result
      sum = addEpi32(sum, n1);
    }

    int unsquared = vecHaddEpi32(sum) / NetworkQA + Content.OutputBias[outputBucket];

    return (unsquared * NetworkScale) / NetworkQAB;
  }

}
