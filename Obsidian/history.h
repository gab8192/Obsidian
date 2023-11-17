#pragma once

using FromToHistory  = int[COLOR_NB][SQUARE_NB * SQUARE_NB];

using PieceToHistory = int[PIECE_NB * SQUARE_NB];

using ContinuationHistory = int[2][PIECE_NB * SQUARE_NB][PIECE_NB * SQUARE_NB];

inline void addToHistory(int& history, int value) {
  history += value - history * abs(value) / 16384;
}