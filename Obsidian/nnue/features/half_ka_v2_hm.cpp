/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2023 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

//Definition of input features HalfKAv2_hm of NNUE evaluation function

#include "half_ka_v2_hm.h"

#include "../../position.h"

namespace Stockfish::Eval::NNUE::Features {

  // Index of a feature for a given king position and another piece on some square
  template<Color Perspective>
  inline IndexType HalfKAv2_hm::make_index(Square s, Piece pc, Square ksq) {
    return IndexType((int(s) ^ OrientTBL[Perspective][ksq]) + PieceSquareIndex[Perspective][pc] + KingBuckets[Perspective][ksq]);
  }

  // Get a list of indices for active features
  template<Color Perspective>
  void HalfKAv2_hm::append_active_indices(
    const Position& pos,
    IndexList& active
  ) {
    Square ksq = pos.kingSquare(Perspective);
    Bitboard bb = pos.pieces();
    while (bb)
    {
      Square s = popLsb(bb);
      active.push_back(make_index<Perspective>(s, pos.board[s], ksq));
    }
  }

  // Explicit template instantiations
  template void HalfKAv2_hm::append_active_indices<WHITE>(const Position& pos, IndexList& active);
  template void HalfKAv2_hm::append_active_indices<BLACK>(const Position& pos, IndexList& active);

}  // namespace Stockfish::Eval::NNUE::Features
