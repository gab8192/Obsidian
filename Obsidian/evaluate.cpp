#include "evaluate.h"
#include "endgame.h"
#include "tuning.h"

#include <iostream>

using namespace std;

namespace Eval {

  constexpr int ScalingValue[PIECE_NB] = { 0, 8, 33, 33, 37, 80, 0, 0,
                                           0, 8, 33, 33, 37, 80, 0, 0 };

  constexpr int full = ScalingValue[PAWN] * 16 + (ScalingValue[KNIGHT] + ScalingValue[BISHOP] + ScalingValue[ROOK]) * 4 +
    ScalingValue[QUEEN] * 2;

  DEFINE_PARAM(val0, 11000, 1000, 30000);
  DEFINE_PARAM(val1, 19000, 1000, 50000);

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

      // 0.7 -> 1.05
      v = Score(v * (material + 1400) / 2000);
    }

    v = Score(v * (200 - pos.halfMoveClock) / 200);

    return v;
  }

#undef pos
}