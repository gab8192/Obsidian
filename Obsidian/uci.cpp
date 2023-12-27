#include "uci.h"
#include "bench.h"
#include "evaluate.h"
#include "move.h"
#include "movegen.h"
#include "nnue.h"
#include "search.h"
#include "threads.h"
#include "tt.h"
#include "tuning.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
using namespace Threads;

std::vector<uint64_t> prevPositions;

namespace {

  // Initial position
  const char* StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

  void position(Position& pos, NNUE::Accumulator& acc, istringstream& is) {
    Move m;
    string token, fen;

    is >> token;

    if (token == "startpos")
    {
      fen = StartFEN;
      is >> token; // Consume the "moves" token, if any
    }
    else if (token == "fen")
      while (is >> token && token != "moves")
        fen += token + " ";
    else
      return;

    pos.setToFen(fen, acc);

    prevPositions.clear();
    prevPositions.push_back(pos.key);

    // Parse the move list, if any
    while (is >> token && (m = UCI::to_move(pos, token)) != MOVE_NONE)
    {
      int dirtyCount = 0;
      DirtyPiece dirtyPieces[4];
      pos.doMove(m, dirtyPieces, dirtyCount);

      // If this move reset the half move clock, we can ignore and forget all the previous position
      if (pos.halfMoveClock == 0)
        prevPositions.clear();

      prevPositions.push_back(pos.key);
    }

    // Remove the last position because it is equal to the current position
    prevPositions.pop_back();
  }

  void newGame() {

    TT::clear();

    for (SearchThread* st : Threads::searchThreads)
      st->resetHistories();
  }

  void bench() {
    constexpr int posCount = sizeof(BenchPositions) / sizeof(char*);

    setThreadCount(1);

    uint64_t totalNodes = 0;
    clock_t elapsed = 0;
    Search::doingBench = true;

    NNUE::Accumulator tempAccumulator;

    for (int i = 0; i < posCount; i++) 
    {
      searchSettings = Search::Settings();
      searchSettings.depth = 13;
      
      istringstream posStr(BenchPositions[i]);
      position(searchSettings.position, tempAccumulator, posStr);

      newGame();

      // Start search
      searchSettings.startTime = timeMillis();
      startSearch();

      // And wait for it to finish..
      Threads::waitForSearch();

      if (i >= 5) {
        // Skip the first 5 positions from nps calculation

        elapsed += Search::lastSearchTimeSpan;

        totalNodes += mainThread()->nodesSearched;
      }
    }

    cout << totalNodes << " nodes " << (totalNodes * 1000 / elapsed) << " nps" << endl;

    Search::doingBench = false;
  }

  // setoption() is called when the engine receives the "setoption" UCI command.
  // The function updates the UCI option ("name") to the given value ("value").

  void setoption(istringstream& is) {
    string token, name, value;

    is >> token; // Consume the "name" token

    // Read the option name (can contain spaces)
    while (is >> token && token != "value")
      name += (name.empty() ? "" : " ") + token;

    // Read the option value (can contain spaces)
    while (is >> token)
      value += (value.empty() ? "" : " ") + token;

    if constexpr (doTuning) {
      EngineParam* param = findParam(name);
      if (param) {
        param->value = std::stoi(value);
        Search::initLmrTable();
        return;
      }
    }

    if (Options.count(name))
      Options[name] = value;
    else
      cout << "No such option: " << name << endl;
  }

  // go() is called when the engine receives the "go" UCI command. The function
  // sets the thinking time and other parameters from the input string, then starts
  // with a search.

  void go(Position& pos, istringstream& is) {

    Threads::waitForSearch();

    string token;

    searchSettings = Search::Settings();
    searchSettings.startTime = timeMillis();
    searchSettings.position = pos;
    searchSettings.prevPositions = prevPositions;

    int perftPlies = 0;

    while (is >> token)
      if (token == "wtime")     is >> searchSettings.time[WHITE];
      else if (token == "btime")     is >> searchSettings.time[BLACK];
      else if (token == "winc")      is >> searchSettings.inc[WHITE];
      else if (token == "binc")      is >> searchSettings.inc[BLACK];
      else if (token == "movestogo") is >> searchSettings.movestogo;
      else if (token == "depth")     is >> searchSettings.depth;
      else if (token == "nodes")     is >> searchSettings.nodes;
      else if (token == "movetime")  is >> searchSettings.movetime;
      else if (token == "perft")     is >> perftPlies;

    if (perftPlies) {
      clock_t begin = timeMillis();
      int64_t nodes = Search::perft<true>(pos, perftPlies);
      clock_t took = timeMillis() - begin;

      std::cout << "nodes: " << nodes << std::endl;
      std::cout << "time: " << took << std::endl;
      std::cout << "nps: " << int(nodes * 1000 / took) << std::endl;
      return;
    }
    else {
      Threads::startSearch();
    }
  }

}

void UCI::loop(int argc, char* argv[]) {

  string token, cmd;

  NNUE::Accumulator tempAccumulator;
  Position pos;

  pos.setToFen(StartFEN, tempAccumulator);

  for (int i = 1; i < argc; ++i)
    cmd += std::string(argv[i]) + " ";

  do {
    if (argc == 1 && !getline(cin, cmd))
      cmd = "quit";

    istringstream is(cmd);

    token.clear(); // Do not crash when given a blank line
    is >> skipws >> token;

    if (token == "quit"
      || token == "stop") {

      stopSearch();
    }

    else if (token == "uci") {
      cout << "id name Obsidian " << engineVersion
        << "\nid author gabe"
        << Options
        << "\n" << paramsToUci()
        << "uciok" << endl;
    }
    else if (token == "bench")      bench();
    else if (token == "setoption")  setoption(is);
    else if (token == "go")         go(pos, is);
    else if (token == "position")   position(pos, tempAccumulator, is);
    else if (token == "ucinewgame") newGame();
    else if (token == "isready")    cout << "readyok" << endl;
    else if (token == "d")        cout << pos << endl;
    else if (token == "tune")     cout << paramsToSpsaInput();
    else if (token == "eval") {
      Score eval = Eval::evaluate(pos, tempAccumulator);
      if (pos.sideToMove == BLACK)
        eval = -eval;
      cout << "Evaluation: " << UCI::to_cp(eval) << endl;
    }
    else if (!token.empty() && token[0] != '#')
      cout << "Unknown command: '" << cmd << "'." << endl;

  } while (token != "quit" && argc == 1); // The command-line arguments are one-shot
}


/// Turns a Value to an integer centipawn number,
/// without treatment of mate and similar special scores.
int UCI::to_cp(Score v) {

  return 100 * v / 220;
}

string UCI::score(Score v) {

  assert(-SCORE_INFINITE < v && v < SCORE_INFINITE);

  stringstream ss;

  if (abs(v) < TB_WIN_IN_MAX_PLY)
    ss << "cp " << UCI::to_cp(v);
  else {
    if (v > 0)
      ss << "mate " << (CHECKMATE - v + 1) / 2;
    else
      ss << "mate " << (-CHECKMATE - v) / 2;
  }

  return ss.str();
}

std::string UCI::square(Square s) {
  return std::string{ char('a' + file_of(s)), char('1' + rank_of(s)) };
}


string UCI::move(Move m) {

  if (m == MOVE_NONE)
    return "(none)";

  string move = UCI::square(move_from(m)) + UCI::square(move_to(m));

  if (move_type(m) == MT_PROMOTION)
    move += "  nbrq"[promo_type(m)];

  return move;
}

Move UCI::to_move(const Position& pos, string& str) {

  if (str.length() == 5)
    str[4] = char(tolower(str[4])); // The promotion piece character must be lowercased

  MoveList moves;
  getPseudoLegalMoves(pos, &moves);

  for (const auto& m : moves)
    if (str == UCI::move(m.move))
      return m.move;

  return MOVE_NONE;
}
