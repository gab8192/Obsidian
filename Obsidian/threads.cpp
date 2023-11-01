#include "threads.h"

namespace Threads {

  volatile Search::State searchState;
  Search::Limits searchLimits;

  void waitForSearch() {
    while (searchState != Search::IDLE)
      sleep(1);
  }
}