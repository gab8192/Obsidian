#pragma once

#include "history.h"
#include "search.h"
#include <atomic>
#include <vector>

namespace Threads {

  extern Search::Settings searchSettings;

  extern std::vector<Search::Thread*> searchThreads;

  extern std::atomic<bool> searchStopped;

  Search::Thread* mainThread();

  bool isSearchStopped();

  uint64_t totalNodes();

  uint64_t totalTbHits();

  void waitForSearch();

  void startSearch(Search::Settings& settings);

  Search::Settings& getSearchSettings();

  void stopSearch();

  void setThreadCount(int threadCount);
}