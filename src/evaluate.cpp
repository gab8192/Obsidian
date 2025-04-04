#include "evaluate.h"
#include "uci.h"

#include <cmath>
#include <iostream>

namespace Eval {

  int cnt = 0;

  int MaterialValue[5] = {229, 709, 768, 1151, 2247};

  int PawnPSQ[128] = { 10, 10, 10, 10, 10, 10, 10, 10, -24, -7, -16, -80, -17, 10, 40, -23, -43, -22, -41, -41, -25, -22, 20, -27, -28, -10, -26, -4, -12, -14, 12, -24, 13, 12, -3, 5, 35, 17, 46, 29, 166, 192, 181, 149, 140, 165, 199, 164, 268, 256, 248, 223, 214, 195, 180, 243, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, -35, -10, -15, -87, -6, 11, 26, -35, -51, -21, -42, -36, -24, -30, 9, -38, -37, -16, -25, -1, -13, -24, -9, -40, -3, 13, -2, -3, 28, 5, 21, -4, 104, 131, 106, 110, 104, 84, 130, 95, 324, 322, 316, 280, 285, 219, 253, 287, 10, 10, 10, 10, 10, 10, 10, 10, };

  Score evaluate(Position& pos, bool isRootStm) {

    Score s = 0;

    for (int pt = PAWN; pt <= QUEEN; pt++) {
      int numDiff =  BitCount(pos.pieces(WHITE, PieceType(pt)))
                   - BitCount(pos.pieces(BLACK, PieceType(pt)));
      s += numDiff * MaterialValue[pt-1];
    }

    Bitboard wp = pos.pieces(WHITE, PAWN);
    while (wp)
      s += PawnPSQ[popLsb(wp) + 64 * pos.sideToMove];

    Bitboard bp = pos.pieces(BLACK, PAWN);
    while (bp)
      s -= PawnPSQ[(popLsb(bp) ^ 56) + 64 * !pos.sideToMove];

    if (pos.sideToMove == BLACK)
      s = -s;

    // random component
  //  score += cnt - 5;

    //cnt = (cnt + 1) % 11;

    int phase =  3 * BitCount(pos.pieces(KNIGHT))
               + 3 * BitCount(pos.pieces(BISHOP))
               + 5 * BitCount(pos.pieces(ROOK))
               + 12 * BitCount(pos.pieces(QUEEN));

    s = s * (200 + phase) / 256;

  //  if (pos.sideToMove == BLACK)
    //  score = -score;

    // Make sure the evaluation does not mix with guaranteed win/loss scores
    s = std::clamp(s, SCORE_TB_LOSS_IN_MAX_PLY + 1, SCORE_TB_WIN_IN_MAX_PLY - 1);

    return s;
  }

  int* weightsMap[2048];

  void copyWeights(int* dest, const float* src, int num) {
    for (int i = 0; i < num; i++)
      dest[i] = src[i];
  }

  
  Score evaluate(Position& pos, const std::vector<float>& weights) {

    for (int i = 0; i < weights.size(); i++) {
      *(weightsMap[i]) = std::round(weights[i]);
    }

    Score s = 0;

    for (int pt = PAWN; pt <= QUEEN; pt++) {
      int numDiff =  BitCount(pos.pieces(WHITE, PieceType(pt)))
                   - BitCount(pos.pieces(BLACK, PieceType(pt)));
      s += numDiff * MaterialValue[pt-1];
    }

    Bitboard wp = pos.pieces(WHITE, PAWN);
    while (wp)
      s += PawnPSQ[popLsb(wp) + 64 * pos.sideToMove];

    Bitboard bp = pos.pieces(BLACK, PAWN);
    while (bp)
      s -= PawnPSQ[(popLsb(bp) ^ 56) + 64 * !pos.sideToMove];

    return s;
  }
  
  

  void mapWeight(std::vector<float>& vec, int* weight) {
    if (0 == *weight)
      *weight = 1;
    int i = vec.size();
    vec.push_back(*weight);
    weightsMap[i] = weight;
  }

  void mapArray(std::vector<float>& vec, int* arr, int size) {
    for (int i = 0; i < size; i++)
      mapWeight(vec, arr + i);
  }
  
  void registerWeights(std::vector<float>& vec) {
    mapArray(vec, PawnPSQ, 128);
  }

#undef pos
}
