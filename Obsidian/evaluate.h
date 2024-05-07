#pragma once

#include "position.h"

namespace Eval {

  void init();

  /// <returns> A value relative to the side to move </returns>
  Score evaluate(Position& pos, NNUE::Accumulator& accumulator);
}