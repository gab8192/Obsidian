#pragma once

#include "types.h"

// [color][from to]
using MainHistory  = int[COLOR_NB][SQUARE_NB * SQUARE_NB];

// [piece to][piece_type]
using CaptureHistory = int[PIECE_NB * SQUARE_NB][PIECE_TYPE_NB];

// [piece to]
using CounterMoveHistory = Move[PIECE_NB * SQUARE_NB];

// [isCap][piece to][piece to]
using ContinuationHistory = int[2][PIECE_NB * SQUARE_NB][PIECE_NB * SQUARE_NB];

inline void addToHistory(int& history, int value) {
  history += value - history * abs(value) / 16384;
}