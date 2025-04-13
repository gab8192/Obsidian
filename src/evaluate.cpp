#include "evaluate.h"
#include "uci.h"

#include <iostream>

namespace Eval {

  int cnt = 0;

  Score evaluate(Position& pos, bool isRootStm) {

    Score score = 0;

    for (PieceType pt = PAWN; pt <= QUEEN; ++pt) {
      int numDiff =  BitCount(pos.pieces(WHITE, pt))
                   - BitCount(pos.pieces(BLACK, pt));
      score += numDiff * PIECE_VALUE[pt] * 24 / 10;
    }

    if (pos.sideToMove == BLACK)
      score = -score;
  
    score += 60 * (2 * isRootStm - 1);

      // random component
    score += cnt - 5;

    cnt = (cnt + 1) % 11;

    int phase =  2 * BitCount(pos.pieces(PAWN))
               + 3 * BitCount(pos.pieces(KNIGHT))
               + 3 * BitCount(pos.pieces(BISHOP))
               + 5 * BitCount(pos.pieces(ROOK))
               + 12 * BitCount(pos.pieces(QUEEN));

    score = score * (230 + phase) / 310;

    // Make sure the evaluation does not mix with guaranteed win/loss scores
    score = std::clamp(score, SCORE_TB_LOSS_IN_MAX_PLY + 1, SCORE_TB_WIN_IN_MAX_PLY - 1);

    return score;
  }

}
