#pragma once

#include "types.h"

#define LmrFile "lmrnn.bin"

namespace LmrNN {

  using weight_t = int16_t;

  constexpr int FeaturesWidth = 8;
  constexpr int HiddenWidth = 8;

  void init();

  Score evaluate(bool* inputs);
}