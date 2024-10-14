#include "timeman.h"
#include "search.h"
#include "uci.h"

#include <cfloat>
#include <cmath>

namespace TimeMan {

  /// Calcaulate how much of our time we should use
  void calcOptimumTime(const Search::Settings& settings, Color us,
                       int64_t* optimumTime, int64_t* maximumTime)
  {
    double optScale;

    int overhead = UCI::Options["Move Overhead"];

    int mtg = settings.movestogo ? std::min(settings.movestogo, 50) : 50;

    int64_t timeLeft = std::max<int64_t>(1LL,
      settings.time[us] + settings.inc[us] * (mtg - 1) - overhead * (2 + mtg));

    if (settings.movestogo == 0) {
      optScale = std::min(0.025,
        0.214 * settings.time[us] / double(timeLeft));
    }
    else {
      optScale = std::min(0.95 / mtg,
        0.88 * settings.time[us] / double(timeLeft));
    }

    *optimumTime = optScale * timeLeft;
    *maximumTime = settings.time[us] * 0.8 - overhead;
  }
}