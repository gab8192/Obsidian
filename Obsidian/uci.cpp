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

  const char* TestFENs[] = {
    "rn1qkb1r/p4ppp/1pp1pn2/3p3b/2PP3N/1QN1P2P/PP3PP1/R1B1KB1R w KQkq - 1 9",
    "r2qk2r/ppp1ppbp/3p1np1/3Pn3/2P1P3/2N2B2/PP3PPP/R1BQK2R w KQkq - 1 9",
    "r1bqkb1r/ppp1p1pp/1n1pp3/6N1/2PP4/3n4/PP3PPP/RNBQK2R w KQkq - 0 9",
    "r2q1rk1/ppp2ppp/2np1n2/2b1pb2/2P5/2N1P1PP/PP1PNPB1/R1BQ1RK1 w - - 1 9",
    "r2qk1nr/1ppb2bp/p1np1pp1/4p1B1/B1PPP3/2N2N2/PP3PPP/R2QK2R w KQkq - 0 9",
    "rnb1kb1r/2q2ppp/p2ppn2/1p6/3NPP2/2N5/PPP1B1PP/R1BQ1RK1 w kq b6 0 9",
    "r1bqk2r/p3bppp/2p2n2/n3p1N1/8/8/PPPPBPPP/RNBQK2R w KQkq - 2 9",
    "r1bq1rk1/pp1nppbp/2p2np1/8/2QPP3/2N2N2/PP2BPPP/R1B1K2R w KQ - 2 9",
    "r2q1rk1/pbpnbppp/1p1ppn2/8/2PP4/2N2NP1/PPQ1PPBP/R1B2RK1 w - - 2 9",
    "r1bqk2r/ppp2ppp/2n5/3pp3/2PPn3/P3P3/1PQ2PPP/R1B1KBNR w KQkq - 0 9",
    "r1b1qrk1/ppp1b1pp/2nppn2/5p2/2PP4/2N2NP1/PPQ1PPBP/R1B2RK1 w - - 3 9",
    "r1b1k2r/pppp1ppp/2n5/3QP3/2P2Bn1/q1P2N2/P3PPPP/R3KB1R w KQkq - 1 9",
    "r1b2rk1/ppqnbppp/2pppn2/8/2PPP3/2N2N2/PP2BPPP/R1BQR1K1 w - - 2 9",
    "rnb1k2r/p3ppbp/2p2np1/qp6/3PP3/1QN2N2/PP3PPP/R1B1KB1R w KQkq - 2 9",
    "r1bqk2r/pp2ppbp/2n2np1/3p4/3NP3/2N1B3/PPP1BPPP/R2Q1RK1 w kq - 0 9",
    "r2q1rk1/pp1bppbp/n2p1np1/2p5/2PPP3/2N2NP1/PP3PBP/R1BQ1RK1 w - - 1 9",
    "r1b1k1nr/pp2qppp/2pp4/2b5/2BpPP2/3P4/PPP3PP/RNBQ1RK1 w kq - 1 9",
    "r1bqk2r/ppnnppbp/2p3p1/8/Q2PP3/2N1BN2/PP3PPP/R3KB1R w KQkq - 3 9",
    "r1bqk1nr/pp1n3p/2pb4/3p1pp1/3P4/5NP1/PP1NPPBP/R1BQ1RK1 w kq - 0 9",
    "rn2kb1r/pb3ppp/5q2/1Ppp4/8/5N2/PP2PPPP/R1BQKB1R w KQkq d6 0 9"
  };

  void bench() {
    constexpr int posCount = sizeof(TestFENs) / sizeof(char*);

    int totalNodes = 0;

    searchLimits = Search::Limits();
    searchLimits.depth = 10;

    for (int i = 0; i < posCount; i++) {
      setPositionToFen(Search::position, TestFENs[i], Search::accumulatorStack);

      Search::clear();

      Threads::searchState = Search::RUNNING;

      while (Threads::searchState == Search::RUNNING)
        _mm_pause();

      totalNodes += Search::nodesSearched;
    }

    cout << "\n###\n\n";

    cout << "Average nodes -> " << (totalNodes / posCount) << endl;
    cout << "Total nodes (bench) -> " << (totalNodes) << endl;
  }

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
      else if (token == "bench")      bench();
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
