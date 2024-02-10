#include "movegen.h"

inline void addNormalMovesToList(Square from, Bitboard destinations, MoveList* receiver) {
  while (destinations) {
    receiver->add(createMove(from, popLsb(destinations), MT_NORMAL));
  }
}

void addPromotionTypes(Square from, Square to, MoveList* receiver) {
  receiver->add(createPromoMove(from, to, QUEEN));
  receiver->add(createPromoMove(from, to, ROOK));
  receiver->add(createPromoMove(from, to, BISHOP));
  receiver->add(createPromoMove(from, to, KNIGHT));
}

template<int Drag>
Bitboard shl(Bitboard bb) {
  if (Drag < 0)
    return bb >> -Drag;
  else
    return bb << Drag;
}

template<Color Us>
void addPawnMoves(const Position& pos, Bitboard targets, MoveList* receiver, MoveGenFlags flags) {
  constexpr Bitboard OurRank3BB = Us == WHITE ? Rank3BB : Rank6BB;
  constexpr Bitboard OurRank7BB = Us == WHITE ? Rank7BB : Rank2BB;
  constexpr int Push = Us == WHITE ? 8 : -8;
  constexpr int Diag0 = Us == WHITE ? 9 : -7;
  constexpr int Diag1 = Us == WHITE ? 7 : -9;
  const Bitboard emptySquares = ~ pos.pieces();
  const Bitboard ourPawnsNot7 = pos.pieces(Us, PAWN) & ~OurRank7BB;  
  const Bitboard ourPawns7 = pos.pieces(Us, PAWN) & OurRank7BB;

  if (flags & ADD_QUIETS) {
    // Normal pushes
    Bitboard push1 = shl<Push>(ourPawnsNot7) & emptySquares;
    Bitboard push2 = shl<Push>(push1 & OurRank3BB) & emptySquares & targets;
    push1 &= targets;

    while (push1) {
      Square to = popLsb(push1);
      receiver->add(createMove(to - Push, to, MT_NORMAL));
    }
    while (push2) {
      Square to = popLsb(push2);
      receiver->add(createMove(to - 2*Push, to, MT_NORMAL));
    }
  }

  if (flags & ADD_CAPTURES) {
    // Normal captures
    {
      Bitboard cap0 = shl<Diag0>(ourPawnsNot7 & ~FILE_HBB) & pos.pieces(~Us) & targets;
      Bitboard cap1 = shl<Diag1>(ourPawnsNot7 & ~FILE_ABB) & pos.pieces(~Us) & targets;
      
      while (cap0) {
        Square to = popLsb(cap0);
        receiver->add(createMove(to - Diag0, to, MT_NORMAL));
      }
      while (cap1) {
        Square to = popLsb(cap1);
        receiver->add(createMove(to - Diag1, to, MT_NORMAL));
      }
    }

    // En passant
    if (pos.epSquare != SQ_NONE) {
      Bitboard ourPawnsTakeEp = ourPawnsNot7 & getPawnAttacks(pos.epSquare, ~Us);
      while (ourPawnsTakeEp) {
        Square from = popLsb(ourPawnsTakeEp);
        receiver->add(createMove(from, pos.epSquare, MT_EN_PASSANT));
      }
    }

    // Promotions
    {
      Bitboard push1 = shl<Push>(ourPawns7) & emptySquares & targets;

      Bitboard cap0 = shl<Diag0>(ourPawns7 & ~FILE_HBB) & pos.pieces(~Us) & targets;
      Bitboard cap1 = shl<Diag1>(ourPawns7 & ~FILE_ABB) & pos.pieces(~Us) & targets;

      while (cap0) {
        Square to = popLsb(cap0);
        addPromotionTypes(to - Diag0, to, receiver);
      }
      while (cap1) {
        Square to = popLsb(cap1);
        addPromotionTypes(to - Diag1, to, receiver);
      }
      while (push1) {
        Square to = popLsb(push1);
        addPromotionTypes(to - Push, to, receiver);
      }
    }
  }
}

void getStageMoves(const Position& pos, MoveGenFlags flags, MoveList* moveList) {
  
  const Color us = pos.sideToMove, them = ~us;

  const Square ourKing = pos.kingSquare(us);
  const Bitboard ourRank8BB = (us == WHITE ? Rank8BB : Rank1BB);
  const Bitboard ourPieces = pos.pieces(us);
  const Bitboard theirPieces = pos.pieces(them);
  const Bitboard occupied = ourPieces | theirPieces;

  Bitboard targets = 0;
  if (flags & ADD_QUIETS)
    targets |= ~occupied;
  if (flags & ADD_CAPTURES)
    targets |= theirPieces;

  Bitboard kingTargets = targets;
  Bitboard pawnTargets = targets | ourRank8BB;
  
  if (pos.checkers) {
    if (moreThanOne(pos.checkers)) {
      addNormalMovesToList(ourKing, getKingAttacks(ourKing) & targets, moveList);
      return;
    }

    targets      &= BETWEEN_BB[ourKing][getLsb(pos.checkers)];
    pawnTargets  &= BETWEEN_BB[ourKing][getLsb(pos.checkers)];
  }

  const Bitboard pinned = pos.blockersForKing[us] & ourPieces;

  if (us == WHITE)
    addPawnMoves<WHITE>(pos, pawnTargets, moveList, flags);
  else
    addPawnMoves<BLACK>(pos, pawnTargets, moveList, flags);

  if ((flags & ADD_QUIETS) && !pos.checkers) {
    if (us == WHITE) {
      if (pos.castlingRights & WHITE_OO) {
        if (!(CASTLING_PATH[WHITE_OO] & occupied)) {
          moveList->add(createCastlingMove(WHITE_OO));
        }
      }
      if (pos.castlingRights & WHITE_OOO) {
        if (!(CASTLING_PATH[WHITE_OOO] & occupied)) {
          moveList->add(createCastlingMove(WHITE_OOO));
        }
      }
    }
    else {
      if (pos.castlingRights & BLACK_OO) {
        if (!(CASTLING_PATH[BLACK_OO] & occupied)) {
          moveList->add(createCastlingMove(BLACK_OO));
        }
      }
      if (pos.castlingRights & BLACK_OOO) {
        if (!(CASTLING_PATH[BLACK_OOO] & occupied)) {
          moveList->add(createCastlingMove(BLACK_OOO));
        }
      }
    }
  }

  Bitboard knights = ourPieces & pos.pieces(KNIGHT) & ~pinned;
  while (knights) {
    Square from = popLsb(knights);
    addNormalMovesToList(from, getKnightAttacks(from) & targets, moveList);
  }

  Bitboard bishops = ourPieces & pos.pieces(BISHOP, QUEEN);
  while (bishops) {
    Square from = popLsb(bishops);
    Bitboard attacks = getBishopAttacks(from, occupied) & targets;
    if (pinned & from)
      attacks &= LINE_BB[ourKing][from];
    addNormalMovesToList(from, attacks, moveList);
  }

  Bitboard rooks = ourPieces & pos.pieces(ROOK, QUEEN);
  while (rooks) {
    Square from = popLsb(rooks);
    Bitboard attacks = getRookAttacks(from, occupied) & targets;
    if (pinned & from)
      attacks &= LINE_BB[ourKing][from];
    addNormalMovesToList(from, attacks, moveList);
  }

  addNormalMovesToList(ourKing, getKingAttacks(ourKing) & kingTargets, moveList);
}