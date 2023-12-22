#include "evaluate.h"
#include "endgame.h"
#include "tuning.h"

#include <iostream>

using namespace std;

namespace Eval {

  constexpr int ScalingValue[PIECE_NB] = { 0, 100, 500, 500, 500, 1200, 0, 0,
                                           0, 100, 500, 500, 500, 1200, 0, 0 };

  DEFINE_PARAM(val0, 10000, 5000, 15000);
  DEFINE_PARAM(val1, 20000, 10000, 30000);

  Score evaluate(Position& pos, NNUE::Accumulator& accumulator) {

    const bool whiteOnlyKing = pos.pieces(WHITE) == pos.pieces(WHITE, KING);
    const bool blackOnlyKing = pos.pieces(BLACK) == pos.pieces(BLACK, KING);

    if (whiteOnlyKing && blackOnlyKing)
      return DRAW;

    Score v;

    if (whiteOnlyKing != blackOnlyKing) {
      Color strongSide = whiteOnlyKing ? BLACK : WHITE;
      Score strongV = evaluateEndgame(pos, strongSide);

      v = (strongSide == pos.sideToMove ? strongV : -strongV);
    }
    else {
      v = NNUE::evaluate(accumulator, pos.sideToMove);

      int material = 0;

      Bitboard piecesIter = pos.pieces();
      while (piecesIter) material += ScalingValue[pos.board[popLsb(piecesIter)]];

      

      v = Score(v * (material + val0) / val1);
    }

    v = Score(v * (200 - pos.halfMoveClock) / 200);

    return v;
  }

#undef pos
}