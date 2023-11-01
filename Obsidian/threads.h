#pragma once

#include "search.h"

namespace Threads {
  

  extern volatile Search::State searchState;
  extern Search::Limits searchLimits;

  void waitForSearch();
}