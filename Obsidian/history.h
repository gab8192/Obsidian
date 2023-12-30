#pragma once

#include "tt.h"

// [color][from to]
using MainHistory  = int[COLOR_NB][SQUARE_NB * SQUARE_NB];

// [piece to][piece to]
using CaptureHistory = int[PIECE_NB * SQUARE_NB][PIECE_TYPE_NB];

// [piece to]
using PieceToHistory = int[PIECE_NB * SQUARE_NB];

// [piece to]
using CounterMoveHistory = Move[PIECE_NB * SQUARE_NB];

// [isCap][piece to][piece to]
using ContinuationHistory = int[2][PIECE_NB * SQUARE_NB][PIECE_NB * SQUARE_NB];

inline void addToHistory(int& history, int value) {
  history += value - history * abs(value) / 16384;
}

struct EvalCorrectionHistory {
  static constexpr uint64_t N = 4096;

  int data[N];

   uint64_t hash(Key key) {
    return key & (N - 1);
  } 

  Score correctionFor(Key key) {
    const int raw = data[hash(key)];
    return Score(raw / 256);
  }

  void update(Key key, TT::Flag bound, Score delta) {
    constexpr int correctionLimit = 65536;
    constexpr int deltaWeight = 1;
    constexpr int correctionWeight = 255;
    constexpr int totalWeight = deltaWeight + correctionWeight;

    if (bound == TT::FLAG_UPPER && delta <= 0)
      return;
    if (bound == TT::FLAG_LOWER && delta >= 0)
      return;

    int& correction = data[hash(key)];
    int scaledDelta = delta * 256;
    correction = (correction * correctionWeight - scaledDelta * deltaWeight) / 256;
    correction = std::clamp(correction, -correctionLimit, correctionLimit);
  }
};