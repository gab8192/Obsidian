#pragma once

#include "types.h"

namespace Eval {

  /// <summary>
  /// Evaluate a position where one the players has only the king
  /// </summary>
  /// <returns> A value relative to the strong side </returns>
  Value evaluateEndgame(Color strongSide);
}