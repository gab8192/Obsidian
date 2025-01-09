#pragma once

#include "simd.h"
#include "types.h"

#define EvalFile "quantised.bin"

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

  using weight_t = float;

  constexpr int FeaturesWidth = 768;
  constexpr int L1 = 512;
  constexpr int L2 = 16;
  constexpr int L3 = 32;

  constexpr int KingBucketsScheme[] = {
    0,  1,  2,  3,  3,  2,  1,  0,
    4,  5,  6,  7,  7,  6,  5,  4,
    8,  8,  9,  9,  9,  9,  8,  8,
    10, 10, 10, 10, 10, 10, 10, 10,
    11, 11, 11, 11, 11, 11, 11, 11, 
    11, 11, 11, 11, 11, 11, 11, 11, 
    12, 12, 12, 12, 12, 12, 12, 12, 
    12, 12, 12, 12, 12, 12, 12, 12, 
  };
  constexpr int KingBuckets = 13;

  constexpr int OutputBuckets = 8;

  constexpr int NetworkScale = 400;

  struct Accumulator {
    
    alignas(Alignment) weight_t colors[COLOR_NB][L1];

    bool updated[COLOR_NB];
    Square kings[COLOR_NB];
    DirtyPieces dirtyPieces;

    void addPiece(Square kingSq, Color side, Piece pc, Square sq);

    void removePiece(Square kingSq, Color side, Piece pc, Square sq);

    void doUpdates(Square kingSq, Color side, Accumulator& input);

    void reset(Color side);

    void refresh(Position& pos, Color side);
  };

  struct FinnyEntry {
    Bitboard byColorBB[COLOR_NB][COLOR_NB];
    Bitboard byPieceBB[COLOR_NB][PIECE_TYPE_NB];
    Accumulator acc;

    void reset();
  };

  using FinnyTable = FinnyEntry[2][KingBuckets];

  bool needRefresh(Color side, Square oldKing, Square newKing);

  void init();

  Score evaluate(Position& pos, Accumulator& accumulator);
}