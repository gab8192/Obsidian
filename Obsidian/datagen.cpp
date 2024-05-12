#include "datagen.h"
#include "movegen.h"
#include "search.h"
#include "threads.h"
#include "tt.h"
#include "uci.h"
#include <random>

namespace Datagen {

  void playRandomMoves(Position& pos, int numMoves) {
    std::mt19937 gen(__rdtsc());
    for (int i = 0; i < 10; i++) {
      MoveList legals;
      {
        MoveList pseudoLegals;
        getStageMoves(pos, ADD_ALL_MOVES, &pseudoLegals);
        for (const auto& m : pseudoLegals)
          if (pos.isLegal(m.move))
            legals.add(m);
      }
      
      std::uniform_int_distribution<int>  dis(0, legals.size()-1);

      Move move = legals[dis(gen)].move;
      DirtyPieces dp;
      pos.doMove(move, dp);
    }
  }

  bool enoughMaterialToMate(Position& pos, Color side) {
    if (pos.pieces(side, PAWN) || pos.pieces(side, ROOK) || pos.pieces(side, QUEEN))
      return true;

    const Bitboard knights = pos.pieces(side, KNIGHT);
    const Bitboard bishops = pos.pieces(side, BISHOP);

    if (knights && bishops)
      return true;

    return BitCount(knights) >= 3 || BitCount(bishops) >= 2;
  }

  bool anyLegalMove(Position& pos) {
    MoveList pseudoLegals;
    getStageMoves(pos, ADD_ALL_MOVES, &pseudoLegals);
    for (const auto& m : pseudoLegals)
      if (pos.isLegal(m.move))
        return true;
    
    return false;
  }

  constexpr int MAX_GAME_PLY = 400;

  void playGame(Position pos /* copy on purpose */ ) {
    TT::clear();

    int ply = 0;
    Key prevPositions[MAX_GAME_PLY+4];

    Search::Thread* st = Threads::mainThread();
    while (true) {

      if (!anyLegalMove(pos))
        return;

      Search::Settings settings;
      settings.startTime = timeMillis();
      settings.position = pos;
      settings.nodes = 5000;

      TT::nextSearch();   

      Threads::searchSettings = settings;
      Threads::searchStopped = false;
      st->nodesSearched = 0;

      st->startSearch();

      Score score = st->rootMoves[0].score;
      Move move = st->rootMoves[0].move;

      std::cout << pos.toFenString() << " | " << score << " | " << UCI::moveToString(move) << std::endl;

      prevPositions[ply++] = pos.key;

      DirtyPieces dp;
      pos.doMove(move, dp);

      if (pos.halfMoveClock >= 100)
        return;
      if (std::abs(score) >= SCORE_TB_WIN_IN_MAX_PLY)
        return;   
      if (!enoughMaterialToMate(pos, WHITE) && !enoughMaterialToMate(pos, BLACK))
        return;
      for (int i = ply - 4; i >= ply - pos.halfMoveClock; i -= 2)
        if (prevPositions[i] == pos.key)
          return;
    }
  }

  void datagen() {
    Search::infoDisabled = true;
    Search::Thread* st = Threads::mainThread();

    for (int i = 0; i < 10; i++) {
      Position pos;
      pos.setToFen(StartFEN);
      playRandomMoves(pos, 9 + (i & 1)); // 9 or 10 random moves
      pos.halfMoveClock = 0;

      playGame(pos);
    }
  }

}