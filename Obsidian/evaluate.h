#pragma once

#include "position.h"

namespace Eval {

  /// <returns> A value relative to the side to move </returns>
  Value evaluate(Position& pos, NNUE::Accumulator& accumulator);
}