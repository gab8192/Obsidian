#include "mininnue.h"
#include "bitboard.h"
#include "incbin.h"
#include "position.h"
#include "uci.h"

#include <iostream>
#include <fstream>

INCBIN(EmbeddedMiniNNUE, MiniEvalFile);

namespace MiniNNUE {

  constexpr int WeightsPerVec = sizeof(Vec) / sizeof(weight_t);

  struct {
    alignas(Alignment) weight_t FeatureWeights[2][6][64][HiddenWidth];
    alignas(Alignment) weight_t FeatureBiases[HiddenWidth];
    alignas(Alignment) weight_t OutputWeights[2 * HiddenWidth];
                       weight_t OutputBias;
  } Content;

  inline weight_t* featureAddress(Color side, Piece pc, Square sq) {

    return Content.FeatureWeights
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

  void Accumulator::addPiece(Color side, Piece pc, Square sq) {
    multiAdd<HiddenWidth>(colors[side], colors[side], featureAddress(side, pc, sq));
  }

  void Accumulator::removePiece(Color side, Piece pc, Square sq) {
    multiSub<HiddenWidth>(colors[side], colors[side], featureAddress(side, pc, sq));
  }

  void Accumulator::doUpdates(DirtyPieces& dp, Color side, Accumulator& input) {
    
    if (dp.type == DirtyPieces::CASTLING) 
    {
      multiSubAddSubAdd<HiddenWidth>(colors[side], input.colors[side], 
        featureAddress(side, dp.sub0.pc, dp.sub0.sq),
        featureAddress(side, dp.add0.pc, dp.add0.sq),
        featureAddress(side, dp.sub1.pc, dp.sub1.sq),
        featureAddress(side, dp.add1.pc, dp.add1.sq));
    } else if (dp.type == DirtyPieces::CAPTURE) 
    { 
      multiSubAddSub<HiddenWidth>(colors[side], input.colors[side], 
        featureAddress(side, dp.sub0.pc, dp.sub0.sq),
        featureAddress(side, dp.add0.pc, dp.add0.sq),
        featureAddress(side, dp.sub1.pc, dp.sub1.sq));
    } else
    {
      multiSubAdd<HiddenWidth>(colors[side], input.colors[side], 
        featureAddress(side, dp.sub0.pc, dp.sub0.sq),
        featureAddress(side, dp.add0.pc, dp.add0.sq));
    }
  }

  void Accumulator::reset(Color side) {
    memcpy(colors[side], Content.FeatureBiases, sizeof(Content.FeatureBiases));
  }

  void Accumulator::refresh(Position& pos, Color side) {
    reset(side);
    Bitboard occupied = pos.pieces();
    while (occupied) {
      const Square sq = popLsb(occupied);
      addPiece(side, pos.board[sq], sq);
    }
  }

  Bitboard Accumulator::genKey() {
    Bitboard key = 0;

    for (int i = 0; i < 32; i++) {
      if (colors[WHITE][i] > 75)
        key |= (1ULL << i);
    }
    for (int i = 0; i < 32; i++) {
      if (colors[BLACK][i] > 75)
        key |= (1ULL << (i+32));
    }

    using uint128 = unsigned __int128;

    return (uint128(key) * uint128(NN_HISTORY_SIZE)) >> 64;
  }

  void init() {

    memcpy(&Content, gEmbeddedMiniNNUEData, sizeof(Content));

  }

  Score evaluate(Position& pos, Accumulator& accumulator) {

    Vec* stmAcc = (Vec*) accumulator.colors[pos.sideToMove];
    Vec* oppAcc = (Vec*) accumulator.colors[~pos.sideToMove];

    Vec* stmWeights = (Vec*) &Content.OutputWeights[0];
    Vec* oppWeights = (Vec*) &Content.OutputWeights[HiddenWidth];

    const Vec vecZero = vecSetZero();
    const Vec vecQA = vecSet1Epi16(NetworkQA);

    Vec sum = vecSetZero();
    Vec v0, v1;

    for (int i = 0; i < HiddenWidth / WeightsPerVec; ++i) {
      // Side to move
      v0 = maxEpi16(stmAcc[i], vecZero); // clip
      v0 = minEpi16(v0, vecQA); // clip
      v1 = mulloEpi16(v0, stmWeights[i]); // square
      v1 = maddEpi16(v1, v0); // multiply with output layer
      sum = addEpi32(sum, v1); // collect the result

      // Non side to move
      v0 = maxEpi16(oppAcc[i], vecZero);
      v0 = minEpi16(v0, vecQA);
      v1 = mulloEpi16(v0, oppWeights[i]);
      v1 = maddEpi16(v1, v0);
      sum = addEpi32(sum, v1);
    }

    int unsquared = vecHaddEpi32(sum) / NetworkQA + Content.OutputBias;

    return (unsquared * NetworkScale) / NetworkQAB;
  }

}
