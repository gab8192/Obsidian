#pragma once

#include "simd.h"
#include "types.h"

#define EvalFile "net9-epoch5.bin"

using namespace SIMD;

struct Position;

struct SquarePiece {
  Square sq;
  Piece pc;
};

struct DirtyPieces {
  SquarePiece sub0, add0, sub1, add1;

  enum {
    NORMAL, CAPTURE, CASTLING
  } type;
};

namespace NNUE {

  using weight_t = int16_t;

  constexpr int FeaturesWidth = 768;
  constexpr int HiddenWidth = 1024;

  constexpr int KingBucketsScheme[] = {
    1, 1, 0, 0, 0, 0, 1, 1,
    1, 1, 0, 0, 0, 0, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 
    2, 2, 2, 2, 2, 2, 2, 2, 
    3, 3, 3, 3, 3, 3, 3, 3, 
    3, 3, 3, 3, 3, 3, 3, 3, 
    3, 3, 3, 3, 3, 3, 3, 3, 
    3, 3, 3, 3, 3, 3, 3, 3
  };
  constexpr int KingBucketsCount = 4;

  constexpr int NetworkScale = 400;
  constexpr int NetworkQA = 181;
  constexpr int NetworkQB = 64;
  constexpr int NetworkQAB = NetworkQA * NetworkQB;

  bool needRefresh(Color side, Square oldKing, Square newKing);

  struct Accumulator {
    union {
      alignas(Alignment) weight_t colors[COLOR_NB][HiddenWidth];
      alignas(Alignment) weight_t both[COLOR_NB * HiddenWidth];
    };

    void addPiece(Square whiteKing, Square blackKing, Piece pc, Square sq);

    void doUpdates(Square kingSq, Color side, DirtyPieces& dp, Accumulator& input);

    void refresh(Position& pos, Color side);
  };

  void init();

  Score evaluate(Accumulator& accumulator, Color sideToMove);
}
