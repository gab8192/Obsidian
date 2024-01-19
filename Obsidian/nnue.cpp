#include "nnue.h"
#include "bitboard.h"
#include "incbin.h"

#include <iostream>
#include <fstream>

INCBIN(EmbeddedNNUE, EvalFile);

namespace NNUE {

  constexpr int WeightsPerVec = sizeof(SIMD::Vec) / sizeof(weight_t);

  struct {
    alignas(SIMD::Alignment) weight_t OldFeatureWeights[2][6][64][HiddenWidth];
    alignas(SIMD::Alignment) weight_t FeatureBiases[HiddenWidth];
    alignas(SIMD::Alignment) weight_t OutputWeights[2 * HiddenWidth];
                             weight_t OutputBias;
  } Content;

  alignas(SIMD::Alignment) weight_t NewFeatureWeights[PIECE_NB][64][2][HiddenWidth];

  inline weight_t* featureAddress(Piece pc, Square sq) {
    return NewFeatureWeights
            [pc]
            [sq]
            [WHITE];
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

  void Accumulator::reset() {
    for (int c = WHITE; c <= BLACK; ++c)
      memcpy(colors[c], Content.FeatureBiases, sizeof(Content.FeatureBiases));
  }

  void Accumulator::addPiece(Square sq, Piece pc) {
    multiAdd<2*HiddenWidth>(both, both, featureAddress(pc, sq));
  }

  void Accumulator::doUpdates(DirtyPieces dp, Accumulator* input) {
    if (dp.type == DirtyPieces::CASTLING) {

      multiSubAddSubAdd<2*HiddenWidth>(both, input->both, 
        featureAddress(dp.sub0.pc, dp.sub0.sq),
        featureAddress(dp.add0.pc, dp.add0.sq),
        featureAddress(dp.sub1.pc, dp.sub1.sq),
        featureAddress(dp.add1.pc, dp.add1.sq));

    } else if (dp.type == DirtyPieces::CAPTURE) {

      multiSubAddSub<2*HiddenWidth>(both, input->both, 
        featureAddress(dp.sub0.pc, dp.sub0.sq),
        featureAddress(dp.add0.pc, dp.add0.sq),
        featureAddress(dp.sub1.pc, dp.sub1.sq));

    } else {

      multiSubAdd<2*HiddenWidth>(both, input->both, 
        featureAddress(dp.sub0.pc, dp.sub0.sq),
        featureAddress(dp.add0.pc, dp.add0.sq));
          
    }
  }

  void init() {

    memcpy(&Content, gEmbeddedNNUEData, sizeof(Content));

    //int j = & Content.FeatureWeights;

    constexpr int pieceWeightsSize = sizeof(Content.OldFeatureWeights[0][0][0]);

    for (Color pov : {WHITE, BLACK}) {
      for (PieceType pt : {PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING}) {
        for (Square sq = SQ_A1; sq < SQUARE_NB; ++sq) {
          Piece whitePc = make_piece(WHITE, pt);
          Piece blackPc = make_piece(BLACK, pt);

          memcpy(NewFeatureWeights[whitePc][sq][pov], 
            Content.OldFeatureWeights[pov != WHITE][pt-1][relative_square(pov, sq)], pieceWeightsSize);

          memcpy(NewFeatureWeights[blackPc][sq][pov], 
            Content.OldFeatureWeights[pov != BLACK][pt-1][relative_square(pov, sq)], pieceWeightsSize);
        }
      }
    }
  }

  Score evaluate(Accumulator& accumulator, Color sideToMove) {

    Vec* stmAcc = (Vec*) accumulator.colors[sideToMove];
    Vec* oppAcc = (Vec*) accumulator.colors[~sideToMove];

    Vec* stmWeights = (Vec*) &Content.OutputWeights[0];
    Vec* oppWeights = (Vec*) &Content.OutputWeights[HiddenWidth];

    const Vec vecZero = vecSetZero();
    const Vec vecQA = vecSet1Epi16(NetworkQA);

    Vec sum = vecSetZero();
    Vec reg;

    for (int i = 0; i < HiddenWidth / WeightsPerVec; ++i) {
      // Side to move
      reg = maxEpi16(stmAcc[i], vecZero); // clip
      reg = minEpi16(reg, vecQA); // clip
      reg = mulloEpi16(reg, reg); // square
      reg = maddEpi16(reg, stmWeights[i]); // multiply with output layer
      sum = addEpi32(sum, reg); // collect the result

      // Non side to move
      reg = maxEpi16(oppAcc[i], vecZero);
      reg = minEpi16(reg, vecQA);
      reg = mulloEpi16(reg, reg);
      reg = maddEpi16(reg, oppWeights[i]);
      sum = addEpi32(sum, reg);
    }

    int unsquared = vecHaddEpi32(sum) / NetworkQA + Content.OutputBias;

    return Score((unsquared * NetworkScale) / NetworkQAB);
  }

}
