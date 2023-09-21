#include "endgame.h"

namespace Eval {

  inline int corner_distance(Square sq) {
	return edge_distance(file_of(sq)) + edge_distance(rank_of(sq));
  }

  /// <summary>
  /// Evaluate a position where one of the players has only the king
  /// </summary>
  /// <returns> A value relative to the strong side </returns>
  Value evaluateEndgame(Position& pos, Color strongSide) {
	Bitboard strongSidePieces = pos.pieces(strongSide) ^ pos.pieces(strongSide, KING);
	const Square strongKing = pos.kingSquare(strongSide);
	const Square weakKing = pos.kingSquare(~strongSide);

	// Only knights
	if (strongSidePieces == pos.pieces(KNIGHT)
	  && BitCount(strongSidePieces) < 3)
	  return VALUE_DRAW;

	// Only bishops
	if (strongSidePieces == pos.pieces(BISHOP)) {

	  if ((strongSidePieces & DARK_SQUARES_BB)
		&& (strongSidePieces & LIGHT_SQUARES_BB)) {
		// good, we have bishop of both colors
	  }
	  else
		return VALUE_DRAW;
	}

	Value value = Value(2000);

	{
	  Bitboard sspIter = strongSidePieces;
	  while (sspIter) {
		Square sq = popLsb(sspIter);
		value += PieceValue[pos.board[sq]];
	  }
	}

	// Keeping the strong king close to the opponent's king is good in any won endgame
	value -= 20 * SquareDistance[strongKing][weakKing];

	// Pushing the weak king towards a corner is good in any won endgame.
	// It is not possible when having only pawns tho
	if (strongSidePieces & ~pos.pieces(PAWN))
	  value -= 20 * corner_distance(weakKing);

	Bitboard pawns = pos.pieces(PAWN);
	while (pawns) {
	  Square pawn = popLsb(pawns);

	  if (strongKing & passed_pawn_span(strongSide, pawn))
		value += 100 + 30 * relative_rank(strongSide, pawn);

	  value += 25 * (SquareDistance[weakKing][pawn] - SquareDistance[strongKing][pawn]);
	}

	return value;
  }

#undef pos
}