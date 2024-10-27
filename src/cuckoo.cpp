#include "cuckoo.h"
#include "bitboard.h"
#include "move.h"
#include "zobrist.h"

namespace Cuckoo {

  Key keys[8192];
  Move moves[8192];

  void init() {

    memset(keys, 0, sizeof(keys));
    memset(moves, 0, sizeof(moves));

    // Only used to check integrity of cuckoo tables
    int count = 0;

    for (PieceType pt : {KNIGHT, BISHOP, ROOK, QUEEN, KING}) {
      for (Color color : {WHITE, BLACK}) {

        Piece piece = makePiece(color, pt);

        for (Square s1 = SQ_A1; s1 < SQUARE_NB; ++s1) {
          for (Square s2 = s1+1; s2 < SQUARE_NB; ++s2) {
            if (getPieceAttacks(pt, s1, 0) & s2) {

              Move move = createMove(s1, s2, MT_NORMAL);

              Key key = ZOBRIST_PSQ[piece][s1] ^ ZOBRIST_PSQ[piece][s2] ^ ZOBRIST_TEMPO;

              int slot = h1(key);

              while (true) {
                std::swap(keys[slot], key);
                std::swap(moves[slot], move);

                if (!move)
                  break;

                // Use the other slot
                slot = (slot == h1(key)) ? h2(key) : h1(key);
              }

              count++;
            }
          }
        }
      }
    }

    if (count != 3668) {
      std::cout << "oops! cuckoo table is broken." << std::endl;
      exit(-1);
    }
  }

}