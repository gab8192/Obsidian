#include "threads.h"

namespace Threads {

  volatile Search::State searchState;
  Search::Settings searchSettings;

  void waitForSearch() {
    while (searchState != Search::IDLE)
      sleep(1);
  }
}