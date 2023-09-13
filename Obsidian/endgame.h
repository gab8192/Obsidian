#pragma once

#include "position.h"

namespace Eval {

  /// <summary>
  /// Evaluate a position where one the players has only the king
  /// </summary>
  /// <returns> A value relative to the strong side </returns>
  Value evaluateEndgame(Position& pos, Color strongSide);
}