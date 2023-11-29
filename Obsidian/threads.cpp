#include "threads.h"

namespace Threads {

  Search::Settings searchSettings;

  std::vector<SearchThread*> searchThreads;

  SearchThread* mainThread() {
    return searchThreads[0];
  }

  uint64_t totalNodes() {
    uint64_t result = 0;
    for (int i = 0; i < searchThreads.size(); i++)
      result += searchThreads[i]->nodesSearched;
    return result;
  }

  void waitForSearch() {
    for (int i = 0; i < searchThreads.size(); i++)
      while (searchThreads[i]->searchState != Search::IDLE)
        sleep(1);
  }

  void startSearch() {
    for (int i = 0; i < searchThreads.size(); i++)
      searchThreads[i]->searchState = RUNNING;
  }

  void stopSearch() {
    for (int i = 0; i < searchThreads.size(); i++)
      if (searchThreads[i]->searchState == RUNNING)
        searchThreads[i]->searchState = STOP_PENDING;

    waitForSearch();
  }

  void setThreadCount(int threadCount) {
    waitForSearch();

    for (int i = 0; i < searchThreads.size(); i++) {
      searchThreads[i]->stopThread = true;
      searchThreads[i]->thread.join();
      delete searchThreads[i];
    }

    searchThreads.clear();

    for (int i = 0; i < threadCount; i++) {
      searchThreads.push_back(new SearchThread());
    }
  }

  
}