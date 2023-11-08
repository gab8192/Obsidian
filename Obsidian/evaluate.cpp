#include "evaluate.h"
#include "endgame.h"

#include <iostream>

using namespace std;

namespace Eval {

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
    }

    v = Score(v * (200 - pos.halfMoveClock) / 200);

    return v;
  }

#undef pos
}