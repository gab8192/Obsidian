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

  NNUE::Accumulator deltaTable[6][5][SQUARE_NB];

  Accumulator* cachedDelta(Piece attacker, Piece taken, Square to) {
    sizeof(deltaTable);
    if (colorOf(attacker) == WHITE) {
      return & deltaTable[attacker-1][taken-9][to];
    }
    else {
      return & deltaTable[attacker-9][taken-1][to^56];
    }
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

  void Accumulator::reset() {
    for (Color c = WHITE; c <= BLACK; ++c)
      memcpy(colors[c], Content.FeatureBiases, sizeof(Content.FeatureBiases));
  }

  void Accumulator::addPiece(Square sq, Piece pc) {
    multiAdd<HiddenWidth*2>(both, both, featureAddress(pc, sq));
  }

  void Accumulator::removePiece(Square sq, Piece pc) {
    multiSub<HiddenWidth*2>(both, both, featureAddress(pc, sq));
  }
  
  void Accumulator::movePiece(Square from, Square to, Piece pc) {
    multiSubAdd<HiddenWidth*2>(both, both, featureAddress(pc, from), featureAddress(pc, to));
  }

  void Accumulator::doUpdates(DirtyPieces dp, Accumulator* input) {
    const Color side = colorOf(dp.sub0.pc);
    if (dp.type == DirtyPieces::CASTLING) {
      
      multiSubAddSubAdd<2*HiddenWidth>(both, input->both, 
        featureAddress(dp.sub0.pc, dp.sub0.sq),
        featureAddress(dp.add0.pc, dp.add0.sq),
        featureAddress(dp.sub1.pc, dp.sub1.sq),
        featureAddress(dp.add1.pc, dp.add1.sq));

    } else if (dp.type == DirtyPieces::CAPTURE) {

      if (dp.add0.sq == dp.sub1.sq) {
        Accumulator* delta = cachedDelta(dp.add0.pc, dp.sub1.pc, dp.sub1.sq);
        multiAdd<HiddenWidth>(colors[WHITE], input->colors[WHITE], delta->colors[side]);
        multiAdd<HiddenWidth>(colors[BLACK], input->colors[BLACK], delta->colors[~side]);

        multiSub<HiddenWidth*2>(both, both,
          featureAddress(dp.sub0.pc, dp.sub0.sq));
      } else {
        multiSubAddSub<2*HiddenWidth>(both, input->both, 
          featureAddress(dp.sub0.pc, dp.sub0.sq),
          featureAddress(dp.add0.pc, dp.add0.sq),
          featureAddress(dp.sub1.pc, dp.sub1.sq));
      }

    } else {
      multiSubAdd<HiddenWidth*2>(both, input->both, 
        featureAddress(dp.sub0.pc, dp.sub0.sq),
        featureAddress(dp.add0.pc, dp.add0.sq));
    }
  }

  void init() {

    memcpy(&Content, gEmbeddedNNUEData, sizeof(Content));

    // Rebuild the features weights array such that black pov weights are right after their white counterpart
    for (Color pov : {WHITE, BLACK}) {
      for (PieceType pt : {PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING}) {
        for (Square sq = SQ_A1; sq < SQUARE_NB; ++sq) {
          Piece whitePc = make_piece(WHITE, pt);
          Piece blackPc = make_piece(BLACK, pt);

          constexpr int pieceWeightsSize = sizeof(NewFeatureWeights[0][0][0]);

          memcpy(NewFeatureWeights[whitePc][sq][pov], 
            Content.OldFeatureWeights[pov != WHITE][pt-1][relative_square(pov, sq)], pieceWeightsSize);

          memcpy(NewFeatureWeights[blackPc][sq][pov], 
            Content.OldFeatureWeights[pov != BLACK][pt-1][relative_square(pov, sq)], pieceWeightsSize);
        }
      }
    }

    for (PieceType attacker : {PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING}) {
      for (PieceType taken : {PAWN, KNIGHT, BISHOP, ROOK, QUEEN}) {
        for (Square sq = SQ_A1; sq < SQUARE_NB; ++sq) {
          Accumulator* target = & deltaTable[attacker-1][taken-1][sq];
          memset(target, 0, sizeof(Accumulator));

          target->addPiece(sq, make_piece(WHITE, attacker));
          target->removePiece(sq, make_piece(BLACK, taken));
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

    return (unsquared * NetworkScale) / NetworkQAB;
  }

}
