#pragma once

#include "simd.h"
#include "types.h"

#define MiniEvalFile "net_mini.bin"

using namespace SIMD;

struct Position;

struct DirtyPieces;

namespace MiniNNUE {

  using weight_t = int16_t;

  constexpr int FeaturesWidth = 768;
  constexpr int HiddenWidth = 32;

  constexpr int NetworkScale = 400;
  constexpr int NetworkQA = 255;
  constexpr int NetworkQB = 64;
  constexpr int NetworkQAB = NetworkQA * NetworkQB;

  struct Accumulator {
    
    alignas(Alignment) weight_t colors[COLOR_NB][HiddenWidth];

    void addPiece(Color side, Piece pc, Square sq);

    void removePiece(Color side, Piece pc, Square sq);

    void doUpdates(DirtyPieces& dp, Color side, Accumulator& input);

    void reset(Color side);

    void refresh(Position& pos, Color side);

    Bitboard genKey();
  };

  void init();

  Score evaluate(Position& pos, Accumulator& accumulator);
}