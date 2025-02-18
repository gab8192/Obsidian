#pragma once

#include "simd.h"
#include "types.h"

#define EvalFile "supermassive.bin"

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
  constexpr int L1 = 4096;
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
  constexpr int NetworkQA = 255;
  constexpr int NetworkQB = 128;
  constexpr int NetworkQAB = NetworkQA * NetworkQB;

  struct NNWeights {
    alignas(Alignment) int16_t FeatureWeights[KingBuckets][2][6][64][L1];
    alignas(Alignment) int16_t FeatureBiases[L1];

    union {
      alignas(Alignment) int8_t L1Weights[OutputBuckets][L1][L2];
      alignas(Alignment) int8_t L1WeightsAlt[OutputBuckets][L1 * L2];
    }; 
    alignas(Alignment) float L1Biases[OutputBuckets][L2];

    alignas(Alignment) float L2Weights[OutputBuckets][L2][L3];
    alignas(Alignment) float L2Biases[OutputBuckets][L3];

    alignas(Alignment) float L3Weights[OutputBuckets][L3];
    alignas(Alignment) float L3Biases[OutputBuckets];
  };

  extern NNWeights* weightsPool;

  struct Accumulator {

    alignas(Alignment) weight_t colors[COLOR_NB][L1];

    NNWeights* nWeights;

    bool updated[COLOR_NB];
    Square kings[COLOR_NB];
    DirtyPieces dirtyPieces;

    void addPiece(Square kingSq, Color side, Piece pc, Square sq, NNWeights& nWeights);

    void removePiece(Square kingSq, Color side, Piece pc, Square sq, NNWeights& nWeights);

    void doUpdates(Square kingSq, Color side, Accumulator& input, NNWeights& nWeights);

    void reset(Color side, NNWeights& nWeights);

    void refresh(Position& pos, Color side, NNWeights& nWeights);
  };

  struct FinnyEntry {
    Bitboard byColorBB[COLOR_NB][COLOR_NB];
    Bitboard byPieceBB[COLOR_NB][PIECE_TYPE_NB];
    Accumulator acc;

    void reset(NNWeights& nWeights);
  };

  using FinnyTable = FinnyEntry[2][KingBuckets];

  bool needRefresh(Color side, Square oldKing, Square newKing);

  void init();

  Score evaluate(Position& pos, Accumulator& accumulator, NNWeights& nWeights);
}
