#pragma once

#include "simd.h"
#include "types.h"

#define EvalFile "net7.bin"

using namespace SIMD;

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
  constexpr int HiddenWidth = 1536;

  constexpr int NetworkScale = 400;
  constexpr int NetworkQA = 181;
  constexpr int NetworkQB = 64;
  constexpr int NetworkQAB = NetworkQA * NetworkQB;

  struct Accumulator {
    union {
      alignas(Alignment) weight_t colors[COLOR_NB][HiddenWidth];
      alignas(Alignment) weight_t both[COLOR_NB * HiddenWidth];
    };

    DirtyPieces dirtyPieces;
    bool updated;

    void reset();

    void addPiece(Square sq, Piece pc);

    void movePiece(Square from, Square to, Piece pc);

    void doUpdates(Accumulator* input);
  };

  void init();

  Score evaluate(Accumulator& accumulator, Color sideToMove);
}
