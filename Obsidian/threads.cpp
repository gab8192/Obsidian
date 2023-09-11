#include "threads.h"

namespace Threads {
  volatile Search::State searchState;
  Search::Limits searchLimits;

  void* searchThread;
}