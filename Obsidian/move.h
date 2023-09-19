#pragma once

#include "types.h"

/*
* 6 bits for src
* 6 bits for dest
* 2 bits for move type
* 2 bits for extra info ( castling or promotion type )
*/

constexpr Move createMove(Square src, Square dest, MoveType moveType, int extra = 0) {
  return Move( src | (dest << 6) | (moveType << 12) | (extra << 14) );
}

inline Move createPromoMove(Square src, Square dest, PieceType promoType) {
  return createMove(src, dest, MT_PROMOTION, promoType - KNIGHT);
}

inline Move createCastlingMove(CastlingRights type) {

  constexpr Move CastlingMoves[9] = {
    MOVE_NONE,
    createMove(SQ_E1, SQ_G1, MT_CASTLING, 0),
    createMove(SQ_E1, SQ_C1, MT_CASTLING, 1),
    MOVE_NONE,
    createMove(SQ_E8, SQ_G8, MT_CASTLING, 2),
    MOVE_NONE, MOVE_NONE, MOVE_NONE,
    createMove(SQ_E8, SQ_C8, MT_CASTLING, 3)
  };

  return CastlingMoves[type];
}

inline Square getMoveSrc(Move move) {
  // being these the first bits, we do not need to SHR anything 
  return Square(move & 63);
}

inline Square getMoveDest(Move move) {
  return Square((move >> 6) & 63);
}

inline MoveType getMoveType(Move move) {
  return (MoveType)((move >> 12) & 3);
}

inline int getMoveExtra(Move move) {
  //  being these the last bits, we do not need to AND anything
  return move >> 14;
}

inline PieceType getPromoType(Move move) {
  return PieceType(getMoveExtra(move) + KNIGHT);
}

inline CastlingRights getCastlingType(Move move) {
  return CastlingRights(1 << getMoveExtra(move));
}

struct MoveList {
  Move moves[MAX_MOVES];
  int scores[MAX_MOVES];
  int head;

  MoveList() : head(0) {
  }

  inline void add(Move move) {
	moves[head++] = move;
  }

  inline int indexOf(Move move) const {
	for (int i = 0; i < size(); i++) {
	  if (moves[i] == move)
		return i;
	}
	return -1;
  }

  inline int size() const {
	return head;
  }

  inline Move operator[](int index) const {
	return moves[index];
  }

  inline const Move* begin() const {
	return & moves[0];
  }

  inline const Move* end() const {
	return &moves[head];
  }
};