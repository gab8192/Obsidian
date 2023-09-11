#pragma once

#include "position.h"
#include "search.h"

namespace Threads {
  

  extern volatile Search::State searchState;
  extern Search::Limits searchLimits;

  extern void* searchThread;
}