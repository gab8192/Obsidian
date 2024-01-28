#include "nnue.h"
#include "bitboard.h"
#include "incbin.h"

#include <iostream>
#include <fstream>

INCBIN(EmbeddedNNUE, EvalFile);

namespace NNUE {

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

  Accumulator deltaTable[6][SQUARE_NB][SQUARE_NB];

  Accumulator* cachedDelta(Square from, Square to, Piece pc) {
    if (colorOf(pc) == WHITE) {
      return & deltaTable[pc-1][from][to];
    }
    else {
      return & deltaTable[pc-9][from^56][to^56];
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

  
  void Accumulator::movePiece(Square from, Square to, Piece pc) {
    multiSubAdd<HiddenWidth*2>(both, both, featureAddress(pc, from), featureAddress(pc, to));
  }

  void Accumulator::doUpdates(DirtyPieces dp, Accumulator* input) {
    const Color side = colorOf(dp.sub0.pc);
    if (dp.type == DirtyPieces::CASTLING) {
      Accumulator* delta0 = cachedDelta(dp.sub0.sq, dp.add0.sq, dp.add0.pc);
      Accumulator* delta1 = cachedDelta(dp.sub1.sq, dp.add1.sq, dp.add1.pc);

      multiAdd<HiddenWidth>(colors[WHITE], input->colors[WHITE], delta0->colors[side]);
      multiAdd<HiddenWidth>(colors[BLACK], input->colors[BLACK], delta0->colors[~side]);
      multiAdd<HiddenWidth>(colors[WHITE], colors[WHITE], delta1->colors[side]);
      multiAdd<HiddenWidth>(colors[BLACK], colors[BLACK], delta1->colors[~side]);

    } else if (dp.type == DirtyPieces::CAPTURE) {
      if (dp.add0.pc == dp.sub0.pc) {
        Accumulator* delta = cachedDelta(dp.sub0.sq, dp.add0.sq, dp.add0.pc);
        multiAdd<HiddenWidth>(colors[WHITE], input->colors[WHITE], delta->colors[side]);
        multiAdd<HiddenWidth>(colors[BLACK], input->colors[BLACK], delta->colors[~side]);
        
        multiSub<HiddenWidth*2>(both, both,
          featureAddress(dp.sub1.pc, dp.sub1.sq));
      } else {
        multiSubAddSub<HiddenWidth*2>(both, input->both, 
          featureAddress(dp.sub0.pc, dp.sub0.sq),
          featureAddress(dp.add0.pc, dp.add0.sq),
          featureAddress(dp.sub1.pc, dp.sub1.sq));
      }
    } else {
      if (dp.add0.pc == dp.sub0.pc) {
        Accumulator* delta = cachedDelta(dp.sub0.sq, dp.add0.sq, dp.add0.pc);
        multiAdd<HiddenWidth>(colors[WHITE], input->colors[WHITE], delta->colors[side]);
        multiAdd<HiddenWidth>(colors[BLACK], input->colors[BLACK], delta->colors[~side]);
      } else {
        multiSubAdd<HiddenWidth*2>(both, input->both, 
          featureAddress(dp.sub0.pc, dp.sub0.sq),
          featureAddress(dp.add0.pc, dp.add0.sq));
      }  
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

    for (PieceType pt : {PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING}) {
      for (Square s1 = SQ_A1; s1 < SQUARE_NB; ++s1) {
        for (Square s2 = SQ_A1; s2 < SQUARE_NB; ++s2) {
          Accumulator* target = & deltaTable[pt-1][s1][s2];
          memset(target, 0, sizeof(Accumulator));

          target->movePiece(s1, s2, make_piece(WHITE, pt));
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

  Score evaluateWithMove(Accumulator& accumulator, Color sideToMove, 
                         Square from, Square to, Piece piece) 
  {
    Vec* stmAcc = (Vec*) accumulator.colors[sideToMove];
    Vec* oppAcc = (Vec*) accumulator.colors[~sideToMove];

    Vec* stmWeights = (Vec*) &Content.OutputWeights[0];
    Vec* oppWeights = (Vec*) &Content.OutputWeights[HiddenWidth];

    NNUE::Accumulator* moveDelta = NNUE::cachedDelta(from, to, piece);
    Vec* moveDeltaWhite = (Vec*) moveDelta->colors[WHITE];
    Vec* moveDeltaBlack = (Vec*) moveDelta->colors[BLACK];

    const Vec vecZero = vecSetZero();
    const Vec vecQA = vecSet1Epi16(NetworkQA);

    Vec sum = vecSetZero();
    Vec reg;

    for (int i = 0; i < HiddenWidth / WeightsPerVec; ++i) {
      // Side to move
      reg = addEpi16(stmAcc[i], moveDeltaWhite[i]);
      reg = maxEpi16(reg, vecZero); // clip
      reg = minEpi16(reg, vecQA); // clip
      reg = mulloEpi16(reg, reg); // square
      reg = maddEpi16(reg, oppWeights[i]); // multiply with (flipped!) output layer
      sum = addEpi32(sum, reg); // collect the result

      // Non side to move
      reg = addEpi16(oppAcc[i], moveDeltaBlack[i]);
      reg = maxEpi16(reg, vecZero);
      reg = minEpi16(reg, vecQA);
      reg = mulloEpi16(reg, reg);
      reg = maddEpi16(reg, stmWeights[i]);
      sum = addEpi32(sum, reg);
    }

    int unsquared = vecHaddEpi32(sum) / NetworkQA + Content.OutputBias;

    // flip sign
    return - (unsquared * NetworkScale) / NetworkQAB;
  }
}
