#pragma once

#include "search.h"

namespace Threads {
  

  extern volatile Search::State searchState;
  extern Search::Settings searchSettings;

  void waitForSearch();
}