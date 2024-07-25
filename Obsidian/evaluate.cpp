#include "evaluate.h"

#include <iostream>

namespace Eval {

  constexpr int bpScale[] = {0, 0, 250, 240, 225, 200, 165, 125, 75};

  Score evaluate(Position& pos, NNUE::Accumulator& accumulator) {

    Score score = NNUE::evaluate(pos, accumulator);

    int blockedPawns = BitCount( pos.pieces(WHITE, PAWN) & (pos.pieces(BLACK, PAWN) >> 8) );

    if (blockedPawns >= 2)
      score = score * bpScale[blockedPawns] / 256;

    int phase =  3 * BitCount(pos.pieces(KNIGHT))
               + 3 * BitCount(pos.pieces(BISHOP))
               + 5 * BitCount(pos.pieces(ROOK))
               + 12 * BitCount(pos.pieces(QUEEN));    

    score = score * (200 + phase) / 256;           

    // Scale down as 50 move rule approaches
    score = score * (200 - pos.halfMoveClock) / 200;

    // Make sure the evaluation does not mix with guaranteed win/loss scores
    score = std::clamp(score, SCORE_TB_LOSS_IN_MAX_PLY + 1, SCORE_TB_WIN_IN_MAX_PLY - 1);

    return score;
  }

#undef pos
}