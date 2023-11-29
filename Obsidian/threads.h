#pragma once

#include "history.h"
#include "search.h"
#include <vector>

using namespace Search;

namespace Threads {

  extern Settings searchSettings;

  extern std::vector<SearchThread*> searchThreads;

  SearchThread* mainThread();

  uint64_t totalNodes();

  void waitForSearch();

  void startSearch();

  void stopSearch();

  void setThreadCount(int threadCount);
}