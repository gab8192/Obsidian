#include "evaluate.h"

#include <iostream>

using namespace std;

namespace Eval {

  Score evaluate(Position& pos, NNUE::Accumulator& accumulator) {

    Score score = NNUE::evaluate(accumulator, pos.sideToMove);

    // Scale down as 50 move rule approaches
    score = Score(score * (200 - pos.halfMoveClock) / 200);

    // Make sure the evaluation does not mix with guaranteed win/loss scores
    score = std::clamp(score, TB_LOSS_IN_MAX_PLY + 1, TB_WIN_IN_MAX_PLY - 1);

    return score;
  }

#undef pos
}