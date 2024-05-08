#include "threads.h"
#include <atomic>

namespace Threads {

  Search::Settings searchSettings;

  std::vector<Search::Thread*> searchThreads;

  std::atomic<bool> searchStopped;

  Search::Thread* mainThread() {
    return searchThreads[0];
  }

  bool isSearchStopped() {
    return searchStopped.load(std::memory_order_relaxed);
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

  void waitForSearch(bool waitMain) {
    for (int i = !waitMain; i < searchThreads.size(); i++) {
      Search::Thread* st = searchThreads[i];
      std::unique_lock lock(st->mutex);
      st->cv.wait(lock, [&] { return !st->searching; });
    }
  }

  void startSearch(Search::Settings& settings) {
    searchSettings = settings;
    searchStopped = false;
    for (int i = 0; i < searchThreads.size(); i++) {
      Search::Thread* st = searchThreads[i];
      st->nodesSearched = 0;
      st->tbHits = 0;
      st->completeDepth = 0;
    }
    for (int i = 0; i < searchThreads.size(); i++) {
      Search::Thread* st = searchThreads[i];
      st->searching = true;
      st->cv.notify_all();
    }
  }

  Search::Settings& getSearchSettings() {
    return searchSettings;
  }

  void stopSearch() {
    searchStopped = true;
  }

  void setThreadCount(int threadCount) {
    waitForSearch();

    for (int i = 0; i < searchThreads.size(); i++) {
      searchThreads[i]->exitThread = true;
      searchThreads[i]->searching = true; // <-- the predicate
      searchThreads[i]->cv.notify_all();
      searchThreads[i]->thread.join();
      delete searchThreads[i];
    }

    searchThreads.clear();

    for (int i = 0; i < threadCount; i++) {
      searchThreads.push_back(new Search::Thread());
    }
  }

}