#include "timeman.h"
#include "search.h"
#include "uci.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace TimeMan {

  /// Calcaulate how much of our time we should use.
  // Its important both to play good moves and to not run out of clock

  clock_t calcOptimumTime(Search::Limits& limits, Color us) {

    // optScale is a percentage of available time to use for the current move.
    double optScale;

    int mtg = limits.movestogo ? std::min(limits.movestogo, 50) : 50;

    clock_t timeLeft = std::max(clock_t(1),
      limits.time[us] + limits.inc[us] * (mtg - 1) - 10 * (2 + mtg));

    double optExtra = std::clamp(1.0 + 12.0 * limits.inc[us] / limits.time[us], 1.0, 1.12);

    if (limits.movestogo == 0) {
      optScale = std::min(0.024,
        0.2 * limits.time[us] / double(timeLeft))
        * optExtra;
    }
    else {
      optScale = std::min(0.966 / mtg,
        0.88 * limits.time[us] / double(timeLeft));
    }

    return clock_t(optScale * timeLeft);
  }
}