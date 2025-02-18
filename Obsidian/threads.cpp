#include "threads.h"
#include "obsnuma.h"
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

  std::vector<cpu_set_t> makeNodeMappings() {

    std::vector<cpu_set_t> mappings;

    for (int node = 0; node < numaNodeCount(); node++) {
      // See what CPUs are associated with this node
      bitmask* cpuMask = numa_allocate_cpumask();
      numa_node_to_cpus(node, cpuMask);
      
      cpu_set_t cpus;
      CPU_ZERO(&cpus);

      for (int i = 0; i < cpuMask->size; i++)
        if (numa_bitmask_isbitset(cpuMask, i))
          CPU_SET(i, &cpus);

      numa_free_cpumask(cpuMask);
      mappings.push_back(cpus);
    }
    return mappings;
  }

  // A map of numa node index -> CPUs
  std::vector<cpu_set_t> nodeMappings;

  std::atomic<int> startedThreadsCount;

  void threadEntry(int index) {
    // Before doing anything else, bind this thread
    startedThreadsCount++;
    searchThreads[index]->idleLoop();
  }

  void setThreadCount(int threadCount) {
    waitForSearch();

    for (int i = 0; i < searchThreads.size(); i++) {
      searchThreads[i]->exitThread = true;
      searchThreads[i]->searching = true; // <-- the predicate
      searchThreads[i]->cv.notify_all();
      stdThreads[i]->join();
      delete searchThreads[i];
      delete stdThreads[i];
    }

    // Every thread from before, is now destroyed

    // We could do this just once at startup, but w/e
    nodeMappings = makeNodeMappings();

    searchThreads.resize(threadCount);
    stdThreads.resize(threadCount);

    startedThreadsCount = 0;

     for (int i = 0; i < threadCount; i++) {
      searchThreads[i] = new Search::Thread(* NNUE::weightsPool);
      stdThreads[i] = new std::thread(threadEntry, i);

      int node = i % numaNodeCount();
      pthread_setaffinity_np(stdThreads[i]->native_handle(),
            sizeof(cpu_set_t), & nodeMappings[node]);
    }

    while (startedThreadsCount < threadCount) {
      // This is necessary because some Search::Thread(s) may not be ready yet.
      // TODO replace this spin with something cleaner
      // - this will take like a millisecond, all threads are started immediately
    }
  }

}
