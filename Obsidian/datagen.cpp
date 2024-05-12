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
    for (int i = 0; i < numMoves; i++) {
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

  void playGame(Position pos /* copy on purpose */, int ply) {
    TT::clear();

    Key prevPositions[MAX_GAME_PLY+4];

    Search::Thread* st = Threads::mainThread();
    while (true) {

      TT::nextSearch();   

      Search::Settings settings;
      settings.startTime = timeMillis();
      settings.position = pos;
      settings.nodes = 5000;

      Threads::searchSettings = settings;
      Threads::searchStopped = false;
      st->nodesSearched = 0;

      st->startSearch();

      Score score = st->rootMoves[0].score;
      Move move = st->rootMoves[0].move;

      if (pos.sideToMove == BLACK) score *= -1;

      bool isCap = move_type(move) == MT_EN_PASSANT || pos.board[move_to(move)];

      if (   ply >= 16
          && !pos.checkers
          && !isCap
          && std::abs(score) < SCORE_TB_WIN_IN_MAX_PLY)
        std::cout << pos.toFenString() << " | " << score << std::endl;

      prevPositions[ply++] = pos.key;

      DirtyPieces dp;
      pos.doMove(move, dp);

      if (pos.halfMoveClock >= 100 || ply >= MAX_GAME_PLY)
        return;
      if (std::abs(score) >= SCORE_TB_WIN_IN_MAX_PLY)
        return;
      for (int i = ply - 4; i >= ply - pos.halfMoveClock; i -= 2)
        if (prevPositions[i] == pos.key)
          return;
      if (!enoughMaterialToMate(pos, WHITE) && !enoughMaterialToMate(pos, BLACK))
        return;
      if (!anyLegalMove(pos))
        return;
    }
  }

  void datagen() {
    Search::infoDisabled = true;
    Search::Thread* st = Threads::mainThread();

    int gameN = 0;
    while (true) {
      Position pos;
      pos.setToFen(StartFEN);
      int ply = 9 + (gameN & 1);
      playRandomMoves(pos, ply); // 9 or 10 random moves
      pos.halfMoveClock = 0;
      if (! anyLegalMove(pos))
        return;

      playGame(pos, ply);

      gameN++;
    }
  }

}