#include "evaluate.h"
#include "endgame.h"
#include "search.h"

#include <algorithm>
#include <iostream>

using namespace Search;
using namespace std;

namespace Eval {

  Value evaluate() {

    Position& pos = Search::position;

    const bool whiteOnlyKing = pos.pieces(WHITE) == pos.pieces(WHITE, KING);
    const bool blackOnlyKing = pos.pieces(BLACK) == pos.pieces(BLACK, KING);

    if (whiteOnlyKing && blackOnlyKing)
      return VALUE_DRAW;

    Value v;

    if (whiteOnlyKing != blackOnlyKing) {
      Color strongSide = whiteOnlyKing ? BLACK : WHITE;
      Value strongV = evaluateEndgame(pos, strongSide);

      v = (strongSide == pos.sideToMove ? strongV : -strongV);

      v = Value(v * (200 - pos.halfMoveClock) / 200);
    }
    else {
      v = NNUE::evaluate(Search::currentAccumulator(), pos.sideToMove);
    }

    return v;
  }

#undef pos
}