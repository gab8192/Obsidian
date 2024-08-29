#include "datagen.h"
#include "movegen.h"
#include "search.h"
#include "threads.h"
#include "tt.h"
#include "uci.h"
#include <random>

namespace Datagen {

  void genLegals(Position& pos, MoveList& legals) {
    MoveList pseudoLegals;
    getStageMoves(pos, ADD_ALL_MOVES, &pseudoLegals);
    for (const auto& m : pseudoLegals)
      if (pos.isLegal(m.move))
        legals.add(m);
  }

  bool playRandomMoves(Position& pos, int numMoves) {
    std::mt19937 gen(__rdtsc());
    for (int i = 0; i < numMoves; i++) {
      MoveList legals;
      genLegals(pos, legals);

      if (! legals.size())
        return false;
      
      std::uniform_int_distribution<int>  dis(0, legals.size()-1);

      Move move = legals[dis(gen)].move;
      DirtyPieces dp;
      pos.doMove(move, dp);
    }
    return true;
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
    MoveList legals;
    genLegals(pos, legals);
    return legals.size();
  }

  constexpr int MAX_GAME_PLY = 400;

  void playGame(Position pos /* copy on purpose */, int ply) {
    TT::clear();

    Key prevPositions[MAX_GAME_PLY+4];

    Search::Thread* st = Threads::mainThread();
    while (true) {
      if (pos.halfMoveClock >= 100 || ply >= MAX_GAME_PLY)
        return;
      for (int i = ply - 4; i >= ply - pos.halfMoveClock; i -= 2)
        if (prevPositions[i] == pos.key)
          return;
      if (!enoughMaterialToMate(pos, WHITE) && !enoughMaterialToMate(pos, BLACK))
        return;
      if (!anyLegalMove(pos))
        return;

      TT::nextSearch();   

      // Intentionally hide previous positions from search
      Search::Settings settings;
      settings.startTime = timeMillis();
      settings.position = pos;
      settings.nodes = 5000;

      Threads::startSync(settings);

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

      if (std::abs(score) > 2000)
        return;
    }
  }

  void datagen() {
    int gameN = 0;
    while (true) {
      Position pos;
      pos.setToFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
      int ply = 9 + (gameN & 1); // 9 or 10 random moves
      if (! playRandomMoves(pos, ply))
        continue;
      pos.halfMoveClock = 0;
      if (! anyLegalMove(pos))
        return;

      playGame(pos, ply);

      gameN++;
    }
  }

}