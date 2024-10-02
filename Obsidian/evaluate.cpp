#include "evaluate.h"

#include <iostream>

namespace Eval {

  Score evaluate(Position& pos, NNUE::Accumulator& accumulator) {

    Score score = NNUE::evaluate(pos, accumulator);

    // Make sure the evaluation does not mix with guaranteed win/loss scores
    score = std::clamp(score, SCORE_TB_LOSS_IN_MAX_PLY + 1, SCORE_TB_WIN_IN_MAX_PLY - 1);

    return score;
  }

#undef pos
}
