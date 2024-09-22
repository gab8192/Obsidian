#pragma once

#include "search.h"

namespace TimeMan {

  void calcOptimumTime(const Search::Settings& settings, Color us,
                       int64_t* optimumTime, int64_t* maximumTime);
}
