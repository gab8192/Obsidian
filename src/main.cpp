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

  Zobrist::init();

  Bitboards::init();

  positionInit();

  Cuckoo::init();

  Search::init();

  UCI::init();

  Threads::setThreadCount(UCI::Options["Threads"]);
  TT::resize(UCI::Options["Hash"]);

  NNUE::loadWeights();

  UCI::loop(argc, argv);

  Threads::setThreadCount(0);

  return 0;
}