#include "timeman.h"
#include "search.h"
#include "uci.h"

#include <cfloat>
#include <cmath>

namespace TimeMan {

  /// Calcaulate how much of our time we should use
  clock_t calcOptimumTime(Search::Settings& settings, Color us) {

    double optScale;

    int mtg = settings.movestogo ? std::min(settings.movestogo, 50) : 50;

    clock_t timeLeft = std::max(clock_t(1),
      settings.time[us] + settings.inc[us] * (mtg - 1) - 20 * (2 + mtg));

    if (settings.movestogo == 0) {
      optScale = std::min(0.025,
        0.214 * settings.time[us] / double(timeLeft));
    }
    else {
      optScale = std::min(0.95 / mtg,
        0.88 * settings.time[us] / double(timeLeft));
    }

    return clock_t(optScale * timeLeft);
  }
}