#pragma once

#include "history.h"
#include "search.h"
#include <vector>

namespace Threads {

  extern std::vector<Search::Thread*> searchThreads;

  Search::Thread* mainThread();

  bool isSearchStopped();

  uint64_t totalNodes();

  uint64_t totalTbHits();

  void waitForSearch(bool waitMain = true);

  void startSearch(Search::Settings& settings);

  Search::Settings& getSearchSettings();

  void stopSearch();

  void setThreadCount(int threadCount);
}