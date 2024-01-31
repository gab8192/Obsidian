#pragma once

#include "search.h"

namespace TimeMan {

  void calcOptimumTime(const Search::Settings& settings, Color us,
                       clock_t* optimumTime, clock_t* maximumTime);
}
