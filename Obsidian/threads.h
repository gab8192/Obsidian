#pragma once

#include "history.h"
#include "search.h"
#include <vector>

namespace Threads {

  extern std::vector<Search::SearchThread*> searchThreads;

  Search::SearchThread* mainThread();

  bool isSearchStopped();

  uint64_t totalNodes();

  uint64_t totalTbHits();

  void waitForSearch();

  void startSearch(Search::Settings& settings);

  Search::Settings& getSearchSettings();

  void stopSearch();

  void setThreadCount(int threadCount);
}