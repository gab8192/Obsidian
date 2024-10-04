// Obsidian.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "cuckoo.h"
#include "threads.h"
#include "tt.h"
#include "uci.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

int main(int argc, char** argv)
{
  std::cout << "Obsidian " << engineVersion << " by Gabriele Lombardo" << std::endl;

  std::cout << numa_available() << std::endl;
  cpu_set_t de;

  if (1) return 0;

  Zobrist::init();

  Bitboards::init();

  positionInit();

  Cuckoo::init();

  Search::init();

  UCI::init(Options);

  Threads::setThreadCount(Options["Threads"]);
  TT::resize(Options["Hash"]);

  NNUE::init();

  UCI::loop(argc, argv);

  Threads::setThreadCount(0);

  return 0;
}