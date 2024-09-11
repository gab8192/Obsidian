#pragma once

#include "types.h"

constexpr int CORRHIST_SIZE = 16384;

constexpr int CORRHIST_LIMIT = 1024;

// [color][from to]
using MainHistory  = int[COLOR_NB][SQUARE_NB * SQUARE_NB];

// [piece to][piece_type]
using CaptureHistory = int[PIECE_NB * SQUARE_NB][PIECE_TYPE_NB];

// [piece to]
using CounterMoveHistory = Move[PIECE_NB * SQUARE_NB];

// [isCap][piece to][piece to]
using ContinuationHistory = int[2][PIECE_NB * SQUARE_NB][PIECE_NB * SQUARE_NB];

// [stm][pawn hash]
using PawnCorrectionHistory = int[2][CORRHIST_SIZE];

inline int getPawnCorrhistIndex(Key pawnKey){
  return pawnKey % CORRHIST_SIZE;
}

inline void addToCorrhist(int& history, int value){
  history += value - history * abs(value) / CORRHIST_LIMIT;
}

inline void addToHistory(int& history, int value) {
  history += value - history * abs(value) / 16384;
}