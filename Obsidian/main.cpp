// Obsidian.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "threads.h"
#include "tt.h"
#include "uci.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

using namespace std;

int main(int argc, char** argv)
{
  cout << "Obsidian " << engineVersion << " by gabe" << endl;

  bitboardsInit();
  positionInit();

  Search::searchInit();

  Threads::searchState = Search::STOPPED;

  UCI::init(Options);

  TT::resize(Options["Hash"]);

  NNUE::load();

  std::thread searchThread(Search::idleLoop, nullptr);

  UCI::loop(argc, argv);

  searchThread.detach();

  return 0;
}