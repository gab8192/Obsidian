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

namespace {

  const char* StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

  std::vector<uint64_t> prevPositions;

  void position(Position& pos, std::istringstream& is) {
    Move m;
    std::string token, fen;

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

    pos.setToFen(fen);

    prevPositions.clear();
    prevPositions.push_back(pos.key);

    // Parse the move list, if any
    while (is >> token && (m = UCI::stringToMove(pos, token)) != MOVE_NONE)
    {
      DirtyPieces dirtyPieces;
      pos.doMove(m, dirtyPieces);

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

    for (Search::Thread* st : Threads::searchThreads)
      st->resetHistories();
  }

  void qc(Position& pos) {
    MoveList checks;
    getQuietChecks(pos, &checks);

    for (int i = 0; i < checks.size(); i++) {
      std::cout << UCI::moveToString(checks[i].move) << std::endl;
    }
  }

  void bench() {
    constexpr int posCount = sizeof(BENCH_POSITIONS) / sizeof(char*);

    Threads::setThreadCount(1);

    uint64_t totalNodes = 0;
    clock_t elapsed = 0;
    Search::doingBench = true;

    for (int i = 0; i < posCount; i++) 
    {
      Search::Settings searchSettings;
      searchSettings.depth = 13;
      
      std::istringstream posStr(BENCH_POSITIONS[i]);
      position(searchSettings.position, posStr);

      newGame();

      // Start search
      searchSettings.startTime = timeMillis();
      Threads::startSearch(searchSettings);

      // And wait for it to finish..
      Threads::waitForSearch();

      if (i >= 5) {
        // Skip the first 5 positions from nps calculation

        elapsed += timeMillis() - searchSettings.startTime;

        totalNodes += Threads::mainThread()->nodesSearched;
      }
    }

    std::cout << totalNodes << " nodes " << (totalNodes * 1000 / elapsed) << " nps" << std::endl;

    Search::doingBench = false;
  }

  void setoption(std::istringstream& is) {
    std::string token, name, value;

    is >> token;

    while (is >> token && token != "value")
      name += (name.empty() ? "" : " ") + token;

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
      std::cout << "No such option: " << name << std::endl;
  }

  void go(Position& pos, std::istringstream& is) {

    clock_t time = timeMillis();

    Threads::waitForSearch();

    std::string token;

    int perftPlies = 0;
    Search::Settings searchSettings;
    searchSettings.startTime = time;
    searchSettings.position = pos;
    searchSettings.prevPositions = prevPositions;

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
      TT::nextSearch();
      Threads::startSearch(searchSettings);
    }
  }

}

void UCI::loop(int argc, char* argv[]) {

  std::string token, cmd;

  
  Position pos;
  pos.setToFen(StartFEN);

  for (int i = 1; i < argc; ++i)
    cmd += std::string(argv[i]) + " ";

  do {
    if (argc == 1 && !std::getline(std::cin, cmd))
      cmd = "quit";

    std::istringstream is(cmd);

    token.clear();
    is >> std::skipws >> token;

    if (token == "quit"
      || token == "stop") {

      Threads::stopSearch();
      Threads::waitForSearch();
    }

    else if (token == "uci") {
      std::cout << "id name Obsidian " << engineVersion
        << "\nid author Gabriele Lombardo"
        << Options
        << "\n" << paramsToUci()
        << "uciok" << std::endl;
    }
    else if (token == "qc")         qc(pos);
    else if (token == "bench")      bench();
    else if (token == "setoption")  setoption(is);
    else if (token == "go")         go(pos, is);
    else if (token == "position")   position(pos, is);
    else if (token == "ucinewgame") newGame();
    else if (token == "isready")    std::cout << "readyok" << std::endl;
    else if (token == "d")          std::cout << pos << std::endl;
    else if (token == "tune")       std::cout << paramsToSpsaInput();
    else if (token == "eval") {
      NNUE::Accumulator tempAcc;
      tempAcc.refresh(pos, WHITE);
      tempAcc.refresh(pos, BLACK);
      Score eval = Eval::evaluate(pos, tempAcc);
      if (pos.sideToMove == BLACK)
        eval = -eval;
      std::cout << "Evaluation: " << UCI::normalizeToCp(eval) 
           << "  (not normalized: " << eval << ")" << std::endl;
    }
    else if (!token.empty() && token[0] != '#')
      std::cout << "Unknown command: '" << cmd << "'." << std::endl;

  } while (token != "quit" && argc == 1);
}

int UCI::normalizeToCp(Score v) {

  return 100 * v / 220;
}

std::string UCI::scoreToString(Score v) {

  assert(-SCORE_INFINITE < v && v < SCORE_INFINITE);

  std::stringstream ss;

  if (abs(v) < SCORE_MATE_IN_MAX_PLY)
    ss << "cp " << UCI::normalizeToCp(v);
  else {
    if (v > 0)
      ss << "mate " << (SCORE_MATE - v + 1) / 2;
    else
      ss << "mate " << (-SCORE_MATE - v) / 2;
  }

  return ss.str();
}

std::string UCI::squareToString(Square s) {
  return std::string{ char('a' + fileOf(s)), char('1' + rankOf(s)) };
}

std::string UCI::moveToString(Move m) {

  if (m == MOVE_NONE)
    return "(none)";

  std::string move = UCI::squareToString(move_from(m)) + UCI::squareToString(move_to(m));

  if (move_type(m) == MT_PROMOTION)
    move += "  nbrq"[promo_type(m)];

  return move;
}

Move UCI::stringToMove(const Position& pos, std::string& str) {

  if (str.length() == 5)
    str[4] = char(tolower(str[4]));

  MoveList moves;
  getStageMoves(pos, ADD_ALL_MOVES, &moves);

  for (const auto& m : moves)
    if (str == UCI::moveToString(m.move))
      return m.move;

  return MOVE_NONE;
}
