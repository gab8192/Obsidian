// Obsidian.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <Windows.h>

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

  bitboardsInit();
  positionInit();

  Search::searchInit();

  Threads::searchState = Search::STOPPED;

  UCI::init(Options);

  TT::resize(Options["Hash"]);

  NNUE::load("net4.nnue");

  Threads::searchThread = CreateThread(NULL, 16 * 1024 * 1024,
	(LPTHREAD_START_ROUTINE) Search::idleLoop, NULL, 0, NULL);

  UCI::loop(argc, argv);

  TerminateThread(Threads::searchThread, 0);

  return 0;
}