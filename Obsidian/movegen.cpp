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
void addPawnMoves(const Position& pos, Bitboard targets, MoveList* receiver, bool doEp = true) {
  constexpr Bitboard OurRank3BB = Us == WHITE ? Rank3BB : Rank6BB;
  constexpr Bitboard OurRank7BB = Us == WHITE ? Rank7BB : Rank2BB;
  constexpr int Push = Us == WHITE ? 8 : -8;
  constexpr int Diag0 = Us == WHITE ? 9 : -7;
  constexpr int Diag1 = Us == WHITE ? 7 : -9;

  const Bitboard occupied = pos.pieces();
  const Bitboard ourPawns = pos.pieces(Us, PAWN) & ~OurRank7BB;  

  Bitboard push1 = shl<Push>(ourPawns) & (~occupied);
  Bitboard push2 = shl<Push>(push1 & OurRank3BB) & (~occupied) & targets;
  push1 &= targets;

  Bitboard cap0 = shl<Diag0>(ourPawns & ~FILE_HBB) & pos.pieces(~Us) & targets;
  Bitboard cap1 = shl<Diag1>(ourPawns & ~FILE_ABB) & pos.pieces(~Us) & targets;

  while (push1) {
    Square to = popLsb(push1);
    receiver->add(createMove(to - Push, to, MT_NORMAL));
  }
  while (push2) {
    Square to = popLsb(push2);
    receiver->add(createMove(to - 2*Push, to, MT_NORMAL));
  }
  while (cap0) {
    Square to = popLsb(cap0);
    receiver->add(createMove(to - Diag0, to, MT_NORMAL));
  }
  while (cap1) {
    Square to = popLsb(cap1);
    receiver->add(createMove(to - Diag1, to, MT_NORMAL));
  }
  if (pos.epSquare != SQ_NONE && doEp) {
    Bitboard ourPawnsTakeEp = ourPawns & getPawnAttacks(pos.epSquare, ~Us);
    while (ourPawnsTakeEp) {
      Square from = popLsb(ourPawnsTakeEp);
      receiver->add(createMove(from, pos.epSquare, MT_EN_PASSANT));
    }
  }
}

void addBlackPawnPromotions(const Position& pos, Bitboard targets, MoveList* receiver) {
  const Bitboard occupied = pos.pieces();
  const Bitboard bPawns = pos.pieces(BLACK, PAWN) & Rank2BB;

  if (bPawns == 0)
    return;

  Bitboard advance1 = (bPawns >> 8) & (~occupied) & targets;

  Bitboard captureEast = ((bPawns & ~FILE_HBB) >> 7) & pos.pieces(WHITE) & targets;
  Bitboard captureWest = ((bPawns & ~FILE_ABB) >> 9) & pos.pieces(WHITE) & targets;

  while (captureEast) {
    Square dest = popLsb(captureEast);
    addPromotionTypes(dest + 7, dest, receiver);
  }
  while (captureWest) {
    Square dest = popLsb(captureWest);
    addPromotionTypes(dest + 9, dest, receiver);
  }
  while (advance1) {
    Square dest = popLsb(advance1);
    addPromotionTypes(dest + 8, dest, receiver);
  }
}

void addWhitePawnPromotions(const Position& pos, Bitboard targets, MoveList* receiver) {
  const Bitboard occupied = pos.pieces();
  const Bitboard wPawns = pos.pieces(WHITE, PAWN) & Rank7BB;

  if (wPawns == 0)
    return;

  Bitboard advance1 = (wPawns << 8) & (~occupied) & targets;

  Bitboard captureEast = ((wPawns & ~FILE_HBB) << 9) & pos.pieces(BLACK) & targets;
  Bitboard captureWest = ((wPawns & ~FILE_ABB) << 7) & pos.pieces(BLACK) & targets;

  while (captureEast) {
    Square dest = popLsb(captureEast);
    addPromotionTypes(dest - 9, dest, receiver);
  }
  while (captureWest) {
    Square dest = popLsb(captureWest);
    addPromotionTypes(dest - 7, dest, receiver);
  }
  while (advance1) {
    Square dest = popLsb(advance1);
    addPromotionTypes(dest - 8, dest, receiver);
  }
}

void getPseudoLegalMoves(const Position& pos, MoveList* moveList) {
  const Color us = pos.sideToMove, them = ~us;

  const Square ourKing = pos.kingSquare(us);

  const Bitboard ourPieces = pos.pieces(us);
  const Bitboard theirPieces = pos.pieces(them);
  const Bitboard occupied = ourPieces | theirPieces;

  Bitboard targets      = ~ourPieces;

  if (pos.checkers) {
    if (moreThanOne(pos.checkers)) {
      addNormalMovesToList(ourKing, getKingAttacks(ourKing) & targets, moveList);
      return;
    }

    targets      &= BETWEEN_BB[ourKing][getLsb(pos.checkers)];
  }

  const Bitboard pinned = pos.blockersForKing[us] & ourPieces;

  if (us == WHITE) {
    addPawnMoves<WHITE>(pos, targets, moveList);
    addWhitePawnPromotions(pos, targets, moveList);
  }
  else {
    addPawnMoves<BLACK>(pos, targets, moveList);
    addBlackPawnPromotions(pos, targets, moveList);
  }

  Bitboard knights = ourPieces & pos.pieces(KNIGHT) & ~pinned;
  while (knights) {
    Square from = popLsb(knights);
    addNormalMovesToList(from, getKnightAttacks(from) & targets, moveList);
  }

  if (!pos.checkers) {
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

  addNormalMovesToList(ourKing, getKingAttacks(ourKing) & ~ourPieces, moveList);
}

void getStageMoves(const Position& pos, bool quiets, MoveList* moveList) {
  
  const Color us = pos.sideToMove, them = ~us;

  const Square ourKing = pos.kingSquare(us);

  const Bitboard ourPieces = pos.pieces(us);
  const Bitboard theirPieces = pos.pieces(them);
  const Bitboard occupied = ourPieces | theirPieces;

  Bitboard targets = quiets ? (~occupied) : (theirPieces);
  Bitboard kingTargets = targets;
  Bitboard promoTargets = ~ourPieces;
  
  if (pos.checkers) {
    if (moreThanOne(pos.checkers)) {
      addNormalMovesToList(ourKing, getKingAttacks(ourKing) & targets, moveList);
      return;
    }

    targets      &= BETWEEN_BB[ourKing][getLsb(pos.checkers)];
    promoTargets &= BETWEEN_BB[ourKing][getLsb(pos.checkers)];
  }

  const Bitboard pinned = pos.blockersForKing[us] & ourPieces;

  if (us == WHITE) {
    addPawnMoves<WHITE>(pos, targets, moveList, !quiets);
    if (!quiets)
      addWhitePawnPromotions(pos, promoTargets, moveList);
  }
  else {
    addPawnMoves<BLACK>(pos, targets, moveList, !quiets);
    if (!quiets)
      addBlackPawnPromotions(pos, promoTargets, moveList);
  }

  if (quiets && !pos.checkers) {
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