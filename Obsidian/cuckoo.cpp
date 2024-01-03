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

        Piece piece = make_piece(color, pt);

        for (Square s1 = SQ_A1; s1 < SQUARE_NB; ++s1) {
          for (Square s2 = s1+1; s2 < SQUARE_NB; ++s2) {
            if (get_piece_attacks(piece, s1, 0) & s2) {

              Move move = createMove(s1, s2, MT_NORMAL);

              Key key = ZobristPsq[piece][s1] ^ ZobristPsq[piece][s2] ^ ZobristTempo;

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
      cout << "oops! cuckoo table is broken." << endl;
      exit(-1);
    }
  }

}