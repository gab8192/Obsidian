#pragma once

inline void addToHistory(int& history, int value) {
  history += value - history * abs(value) / 16384;
}