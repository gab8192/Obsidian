#pragma once

#include "search.h"

namespace TimeMan {

  void calcOptimumTime(Search::Settings& settings, Color us,
                       clock_t* optimumTime, clock_t* maximumTime);
}
