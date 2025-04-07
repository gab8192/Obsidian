#pragma once

#include "types.h"

constexpr int CORRHIST_SIZE = 32768;

constexpr int CORRHIST_LIMIT = 1024;

// [color][from to]
using MainHistory  = int16_t[COLOR_NB][SQUARE_NB * SQUARE_NB];

// [piece to][piece_type]
using CaptureHistory = int16_t[PIECE_NB * SQUARE_NB][PIECE_TYPE_NB];

// [isCap][piece to][piece to]
using ContinuationHistory = int16_t[2][PIECE_NB * SQUARE_NB][PIECE_NB * SQUARE_NB];

// [stm][pawn hash]
using PawnCorrHist = int16_t[CORRHIST_SIZE][2];

// [stm][pawn hash]
using NonPawnCorrHist = int16_t[CORRHIST_SIZE][2];

inline int ChIndex(Key pawnKey){
  return pawnKey % CORRHIST_SIZE;
}

inline void addToCorrhist(int16_t& history, int value){
  history += value - int(history) * abs(value) / CORRHIST_LIMIT;
}

inline void addToHistory(int16_t& history, int value) {
  history += value - int(history) * abs(value) / 16384;
}