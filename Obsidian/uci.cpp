#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "evaluate.h"
#include "move.h"
#include "movegen.h"
#include "nnue.h"
#include "search.h"
#include "threads.h"
#include "uci.h"

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

    setPositionToFen(pos, fen, &Search::accumulatorStack[0]);

    seenPositions.push_back(pos.key);

    // Parse the move list, if any
    while (is >> token && (m = UCI::to_move(pos, token)) != MOVE_NONE)
    {
      pos.doMove(m, & Search::accumulatorStack[0]);
      seenPositions.push_back(pos.key);
    }
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


  // go() is called when the engine receives the "go" UCI command. The function
  // sets the thinking time and other parameters from the input string, then starts
  // with a search.

  void go(istringstream& is) {

    string token;

    searchLimits = Search::Limits();

    searchLimits.startTime = clock();

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
      clock_t begin = clock();
      int nodes = Search::perft<true>(perftPlies);
      clock_t took = clock() - begin;

      std::cout << "nodes: " << nodes << std::endl;
      std::cout << "time: " << took << std::endl;
      std::cout << "nps: " << int(1000.0 * double(nodes) / double(took)) << std::endl;
      return;
    }
    else {
      Threads::searchState = Search::RUNNING;
    }
  }

}

void UCI::loop(int argc, char* argv[]) {

  string token, cmd;

  setPositionToFen(Search::position, StartFEN, & Search::accumulatorStack[0]);

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
      else if (token == "setoption")  setoption(is);
      else if (token == "go")         go(is);
      else if (token == "position")   position(Search::position, is);
      else if (token == "ucinewgame") Search::clear();
      else if (token == "isready")    cout << "readyok" << endl;
      else if (token == "d")        cout << Search::position << endl;
      else if (token == "eval") {
        Value eval = Eval::evaluate();
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

  return 100 * v / 280;
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

  string move;

  if (getMoveType(m) == MT_CASTLING) {
    move = CASTLING_STRING[getCastlingType(m)];
  }
  else {
    move = UCI::square(getMoveSrc(m)) + UCI::square(getMoveDest(m));

    if (getMoveType(m) == MT_PROMOTION)
      move += "  nbrq"[getPromoType(m)];
  }

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
