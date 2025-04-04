#pragma once

#include "position.h"
#include <vector>

namespace Eval {

  /// <returns> A value relative to the side to move </returns>
  Score evaluate(Position& pos, bool isRootStm);

  Score evaluate(Position& pos, const std::vector<float>& weights);

  void registerWeights(std::vector<float>& vec);
}