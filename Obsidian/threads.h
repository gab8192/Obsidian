#pragma once

#include "history.h"
#include "search.h"
#include <vector>

using namespace Search;

namespace Threads {

  extern Settings searchSettings;

  extern std::vector<SearchThread*> searchThreads;

  SearchThread* mainThread();

  State getSearchState();

  void onSearchComplete();

  uint64_t totalNodes();

  void waitForSearch();

  void startSearch();

  void stopSearch(bool wait = true);

  void setThreadCount(int threadCount);
}