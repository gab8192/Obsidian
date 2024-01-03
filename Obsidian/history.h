#pragma once

#include "types.h"

// [color][from to][from threatened]
using MainHistory  = int[COLOR_NB][SQUARE_NB * SQUARE_NB][2];

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