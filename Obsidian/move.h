#pragma once

#include "types.h"

/*
* 6 bits for dest, 6 bits for src, 2 bits for move type, 2 bits for promotion type
*/

inline Move createMove(Square src, Square dest, MoveType moveType) {
  return Move(src | (dest << 6) | (moveType << 12));
}

inline Move createPromoMove(Square src, Square dest, PieceType promoType) {
  return Move(src | (dest << 6) | (MT_PROMOTION << 12) | ((promoType - KNIGHT) << 14));
}

inline Move createCastlingMove(CastlingRights type) {
  return Move(type | (MT_CASTLING << 12));
}

inline CastlingRights getCastlingType(Move move) {
  return CastlingRights(move & 63);
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

inline PieceType getPromoType(Move move) {
  //  being these the last bits, we do not need to AND anything
  return PieceType((move >> 14) + KNIGHT);
}

struct MoveList {
  Move moves[128];
  int scores[128];
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