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

using namespace std;

int main(int argc, char** argv)
{
  cout << "Obsidian " << engineVersion << " by gabe" << endl;

  zobristInit();

  bitboardsInit();

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