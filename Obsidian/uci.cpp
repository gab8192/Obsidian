#include "uci.h"
#include "bench.h"
#include "evaluate.h"
#include "move.h"
#include "movegen.h"
#include "nnue.h"
#include "search.h"
#include "threads.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
using namespace Threads;

std::vector<uint64_t> seenPositions;

namespace {

  // Initial position
  const char* StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

  void position(Position& pos, istringstream& is) {
    seenPositions.clear();

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

    pos.setToFen(fen, &Search::accumulatorStack[0]);

    seenPositions.push_back(pos.key);

    // Parse the move list, if any
    while (is >> token && (m = UCI::to_move(pos, token)) != MOVE_NONE)
    {
      pos.doMove(m, &Search::accumulatorStack[0]);

      // If this move reset the half move clock, we can ignore and forget all the previous position
      if (pos.halfMoveClock == 0)
        seenPositions.clear();

      seenPositions.push_back(pos.key);
    }
  }

  void bench() {
    constexpr int posCount = sizeof(BenchPositions) / sizeof(char*);

    uint64_t totalNodes = 0;

    searchLimits = Search::Limits();
    searchLimits.depth = 13;

    clock_t elapsed = 0;

    Search::printingEnabled = false;

    for (int i = 0; i < posCount; i++) {

      istringstream posStr(BenchPositions[i]);
      position(Search::position, posStr);

      Search::clear();

      Threads::searchState = Search::RUNNING;

      while (Threads::searchState == Search::RUNNING)
        sleep(1);

      if (i >= 5) {
        // Skip the first 5 positions from nps calculation

        elapsed += Search::lastSearchTimeSpan;

        totalNodes += Search::nodesSearched;
      }
    }

    Search::printingEnabled = true;

    cout << totalNodes << " nodes " << (totalNodes * 1000 / elapsed) << " nps" << endl;
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

    if (Options.count(name))
      Options[name] = value;
    else
      cout << "No such option: " << name << endl;
  }

  Move calcBestMove(int depth) {
    // Setup search options
    searchLimits = Search::Limits();
    searchLimits.startTime = timeMillis();
    searchLimits.depth = depth;

    // Do search
    Threads::searchState = Search::RUNNING;
    while (Threads::searchState == Search::RUNNING) sleep(1);

    return Search::lastBestMove;
  }

  bool anyLegalMove() {
    Position& pos = Search::position;

    MoveList moves;
    getPseudoLegalMoves(pos, &moves);

    for (const auto& m : moves)
      if (pos.isLegal(m))
        return true;

    return false;
  }

  void plan(istringstream& is) {
    Position& pos = Search::position;

    int moves, depth;

    is >> moves;
    is >> depth;

    string oldFen = pos.toFenString();

    Search::printingEnabled = false;

    for (int i = 0; i < moves; i++) {
      Move ourBestMove = calcBestMove(depth);

      // Print the best move and play it
      cout << UCI::move(ourBestMove) << " ";
      pos.doMove(ourBestMove, Search::accumulatorStack);

      if (!anyLegalMove()) {
        cout << "#";
        break;
      }

      // Flip the side to move, but if the opponent is in check then play his move
      if (pos.checkers) {
        seenPositions.push_back(pos.key);
        pos.doMove(calcBestMove(depth), Search::accumulatorStack);
      }
      else {
        pos.doNullMove();
      }
      seenPositions.push_back(pos.key);
    }

    Search::printingEnabled = true;

    cout << endl;

    pos.setToFen(oldFen, Search::accumulatorStack);
    seenPositions.clear();
    seenPositions.push_back(pos.key);
  }


  // go() is called when the engine receives the "go" UCI command. The function
  // sets the thinking time and other parameters from the input string, then starts
  // with a search.

  void go(istringstream& is) {

    string token;

    searchLimits = Search::Limits();
    searchLimits.startTime = timeMillis();

    int perftPlies = 0;

    while (is >> token)
      if (token == "wtime")     is >> searchLimits.time[WHITE];
      else if (token == "btime")     is >> searchLimits.time[BLACK];
      else if (token == "winc")      is >> searchLimits.inc[WHITE];
      else if (token == "binc")      is >> searchLimits.inc[BLACK];
      else if (token == "movestogo") is >> searchLimits.movestogo;
      else if (token == "depth")     is >> searchLimits.depth;
      else if (token == "nodes")     is >> searchLimits.nodes;
      else if (token == "movetime")  is >> searchLimits.movetime;
      else if (token == "perft")     is >> perftPlies;

    if (perftPlies) {
      clock_t begin = timeMillis();
      int nodes = Search::perft<true>(perftPlies);
      clock_t took = timeMillis() - begin;

      std::cout << "nodes: " << nodes << std::endl;
      std::cout << "time: " << took << std::endl;
      std::cout << "nps: " << int(nodes * 1000 / took) << std::endl;
      return;
    }
    else {
      Threads::searchState = Search::RUNNING;
    }
  }

}

void UCI::loop(int argc, char* argv[]) {

  string token, cmd;

  Search::position.setToFen(StartFEN, &Search::accumulatorStack[0]);

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

      if (searchState == Search::RUNNING) {
        searchState = Search::STOP_PENDING;
      }
    }

    else if (token == "uci") {
      cout << "id name Obsidian " << engineVersion
        << "\nid author gabe"
        << "\n" << Options
        << "\nuciok" << endl;
    }
    else if (token == "bench")      bench();
    else if (token == "setoption")  setoption(is);
    else if (token == "plan")       plan(is);
    else if (token == "go")         go(is);
    else if (token == "position")   position(Search::position, is);
    else if (token == "ucinewgame") Search::clear();
    else if (token == "isready")    cout << "readyok" << endl;
    else if (token == "d")        cout << Search::position << endl;
    else if (token == "eval") {
      Value eval = Eval::evaluate(Search::position);
      if (Search::position.sideToMove == BLACK)
        eval = -eval;
      cout << "Evaluation: " << UCI::to_cp(eval) << endl;
    }
    else if (!token.empty() && token[0] != '#')
      cout << "Unknown command: '" << cmd << "'. Type help for more information." << endl;

  } while (token != "quit" && argc == 1); // The command-line arguments are one-shot
}


/// Turns a Value to an integer centipawn number,
/// without treatment of mate and similar special scores.
int UCI::to_cp(Value v) {

  return 100 * v / 220;
}

string UCI::value(Value v) {

  assert(-VALUE_INFINITE < v && v < VALUE_INFINITE);

  stringstream ss;

  if (abs(v) < VALUE_TB_WIN_IN_MAX_PLY)
    ss << "cp " << UCI::to_cp(v);
  else {
    if (v > 0)
      ss << "mate " << (VALUE_MATE - v + 1) / 2;
    else
      ss << "mate " << (-VALUE_MATE - v) / 2;
  }

  return ss.str();
}

std::string UCI::square(Square s) {
  return std::string{ char('a' + file_of(s)), char('1' + rank_of(s)) };
}


string UCI::move(Move m) {

  if (m == MOVE_NONE)
    return "(none)";

  string move = UCI::square(getMoveSrc(m)) + UCI::square(getMoveDest(m));

  if (getMoveType(m) == MT_PROMOTION)
    move += "  nbrq"[getPromoType(m)];

  return move;
}

Move UCI::to_move(const Position& pos, string& str) {

  if (str.length() == 5)
    str[4] = char(tolower(str[4])); // The promotion piece character must be lowercased

  MoveList moves;
  getPseudoLegalMoves(pos, &moves);

  for (const auto& m : moves)
    if (str == UCI::move(m))
      return m;

  return MOVE_NONE;
}
