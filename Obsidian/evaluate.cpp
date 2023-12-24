#include "evaluate.h"

#include <iostream>

using namespace std;

namespace Eval {

  Score evaluate(Position& pos, NNUE::Accumulator& accumulator) {

    Score v = NNUE::evaluate(accumulator, pos.sideToMove);

    v = Score(v * (200 - pos.halfMoveClock) / 200);

    return v;
  }

#undef pos
}