#pragma once

#include "simd.h"
#include "types.h"

#define EvalFile "amogus.bin"

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
    0, 0, 1, 1, 1, 1, 0, 0,
    2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3, 
    3, 3, 3, 3, 3, 3, 3, 3, 
    4, 4, 4, 4, 4, 4, 4, 4, 
    4, 4, 4, 4, 4, 4, 4, 4, 
    4, 4, 4, 4, 4, 4, 4, 4, 
    4, 4, 4, 4, 4, 4, 4, 4
  };
  constexpr int KingBuckets = 5;

  constexpr int OutputBuckets = 8;

  constexpr int NetworkScale = 400;
  constexpr int NetworkQA = 255;
  constexpr int NetworkQB = 64;
  constexpr int NetworkQAB = NetworkQA * NetworkQB;

  struct Accumulator {
    union {
      alignas(Alignment) weight_t colors[COLOR_NB][HiddenWidth];
      alignas(Alignment) weight_t both[COLOR_NB * HiddenWidth];
    };

    void addPiece(Square kingSq, Color side, Piece pc, Square sq);

    void removePiece(Square kingSq, Color side, Piece pc, Square sq);

    void doUpdates(Square kingSq, Color side, DirtyPieces& dp, Accumulator& input);

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