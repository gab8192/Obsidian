#include "evaluate.h"
#include "uci.h"

#include <iostream>

namespace Eval {

  int cnt = 0;

  Score evaluate(Position& pos, bool isRootStm, NNUE::Accumulator& accumulator) {

    Score score = 0;

    for (int pt = PAWN; pt <= QUEEN; pt++) {
      int numDiff =  BitCount(pos.pieces(pos.sideToMove, PieceType(pt)))
                   - BitCount(pos.pieces(~pos.sideToMove, PieceType(pt)));
      score += numDiff * PIECE_VALUE[pt] * 22 / 10;
    }

    // random component
    score += cnt - 5;

    cnt = (cnt + 1) % 11;

   // small contempt
   score += isRootStm ? 50 : -50;

    if (isRootStm)
      score += UCI::contemptValue;
    else
      score -= UCI::contemptValue;

    int phase =  3 * BitCount(pos.pieces(KNIGHT))
               + 3 * BitCount(pos.pieces(BISHOP))
               + 5 * BitCount(pos.pieces(ROOK))
               + 12 * BitCount(pos.pieces(QUEEN));

    score = score * (200 + phase) / 256;

    // Make sure the evaluation does not mix with guaranteed win/loss scores
    score = std::clamp(score, SCORE_TB_LOSS_IN_MAX_PLY + 1, SCORE_TB_WIN_IN_MAX_PLY - 1);

    return score;
  }

#undef pos
}
