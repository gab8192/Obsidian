#include "threads.h"
#include "nnue.h"
#include <atomic>

namespace Threads {

  Search::Settings searchSettings;

  std::vector<std::thread*> stdThreads;
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

  void startSearchSingle(Search::Thread* st) {
    st->mutex.lock();
    st->searching = true;
    st->mutex.unlock();
    st->cv.notify_all();
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
    for (int i = 0; i < searchThreads.size(); i++)
      startSearchSingle(searchThreads[i]);
  }

  Search::Settings& getSearchSettings() {
    return searchSettings;
  }

  void stopSearch() {
    searchStopped = true;
  }

  std::atomic<int> startedThreadsCount;

  void threadEntry(int index) {
    searchThreads[index] = new Search::Thread();
    startedThreadsCount++;
    searchThreads[index]->idleLoop();
  }

  void setThreadCount(int threadCount) {
    waitForSearch();

    for (int i = 0; i < searchThreads.size(); i++) {
      searchThreads[i]->exitThread = true;
      startSearchSingle(searchThreads[i]);
      stdThreads[i]->join();
      delete searchThreads[i];
      delete stdThreads[i];
    }

    searchThreads.resize(threadCount);
    stdThreads.resize(threadCount);

    startedThreadsCount = 0;

     for (int i = 0; i < threadCount; i++) {
      stdThreads[i] = new std::thread(threadEntry, i);
    }

    while (startedThreadsCount < threadCount) {
      // This is necessary because some Search::Thread(s) may not be ready yet.
      // TODO replace this spin with something cleaner
      // - this will take like a millisecond, all threads are started immediately
    }
  }

}
