#include "threads.h"

namespace Threads {

  Search::Settings searchSettings;

  std::vector<SearchThread*> searchThreads;

  volatile bool searchStopped;

  SearchThread* mainThread() {
    return searchThreads[0];
  }

  bool isSearchStopped() {
    return searchStopped;
  }

  uint64_t totalNodes() {
    uint64_t result = 0;
    for (int i = 0; i < searchThreads.size(); i++)
      result += searchThreads[i]->nodesSearched;
    return result;
  }

  uint64_t totalTbHits() {
    uint64_t result = 0;
    for (int i = 0; i < searchThreads.size(); i++)
      result += searchThreads[i]->tbHits;
    return result;
  }

  void waitForSearch() {
    for (int i = 0; i < searchThreads.size(); i++) {
      SearchThread* st = searchThreads[i];
      std::unique_lock lock(st->mutex);
      st->cv.wait(lock, [&] { return !st->searching; });
    }
  }

  void onSearchComplete() {
    searchStopped = true;
  }

  void startSearch() {
    searchStopped = false;
    for (int i = 0; i < searchThreads.size(); i++) {
      SearchThread* st = searchThreads[i];
      st->searching = true;
      st->cv.notify_all();
    }
  }

  void stopSearch(bool wait) {
    if (! searchStopped) {
      searchStopped = true;
      if (wait)
        waitForSearch();
    }
  }

  void setThreadCount(int threadCount) {
    waitForSearch();

    for (int i = 0; i < searchThreads.size(); i++) {
      searchThreads[i]->searching = true; // <-- the predicate
      searchThreads[i]->stopThread = true;
      searchThreads[i]->cv.notify_all();
      searchThreads[i]->thread.join();
      delete searchThreads[i];
    }

    searchThreads.clear();

    for (int i = 0; i < threadCount; i++) {
      searchThreads.push_back(new SearchThread());
    }
  }

}