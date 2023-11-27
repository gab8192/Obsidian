#include "movegen.h"

inline void addNormalMovesToList(Square from, Bitboard destinations, MoveList* receiver) {
  while (destinations) {
    receiver->add(createMove(from, popLsb(destinations), MT_NORMAL));
  }
}

void addPromotionTypes(Square src, Square dest, MoveList* receiver) {
  receiver->add(createPromoMove(src, dest, QUEEN));
  receiver->add(createPromoMove(src, dest, ROOK));
  receiver->add(createPromoMove(src, dest, BISHOP));
  receiver->add(createPromoMove(src, dest, KNIGHT));
}

void getWhitePawnMoves(const Position& pos, Bitboard targets, MoveList* receiver, bool doEp = true) {
  const Bitboard allPieces = pos.pieces();
  const Bitboard wPawns = pos.pieces(WHITE, PAWN) & ~Rank7BB;

  Bitboard advance1 = (wPawns << 8) & (~allPieces);
  Bitboard advance2 = ((advance1 & Rank3BB) << 8) & (~allPieces) & targets;
  advance1 &= targets;

  Bitboard captureEast = ((wPawns & ~FILE_HBB) << 9) & pos.pieces(BLACK) & targets;
  Bitboard captureWest = ((wPawns & ~FILE_ABB) << 7) & pos.pieces(BLACK) & targets;

  while (advance1) {
    Square dest = popLsb(advance1);
    receiver->add(createMove(dest - 8, dest, MT_NORMAL));
  }
  while (advance2) {
    Square dest = popLsb(advance2);
    receiver->add(createMove(dest - 16, dest, MT_NORMAL));
  }
  while (captureEast) {
    Square dest = popLsb(captureEast);
    receiver->add(createMove(dest - 9, dest, MT_NORMAL));
  }
  while (captureWest) {
    Square dest = popLsb(captureWest);
    receiver->add(createMove(dest - 7, dest, MT_NORMAL));
  }

  if (pos.epSquare != SQ_NONE && doEp) {
    Bitboard ourPawnsTakeEp = wPawns & get_pawn_attacks(pos.epSquare, BLACK);

    while (ourPawnsTakeEp) {
      Square src = popLsb(ourPawnsTakeEp);
      receiver->add(createMove(src, pos.epSquare, MT_EN_PASSANT));
    }
  }
}

void getBlackPawnMoves(const Position& pos, Bitboard targets, MoveList* receiver, bool doEp = true) {
  const Bitboard allPieces = pos.pieces();
  const Bitboard bPawns = pos.pieces(BLACK, PAWN) & ~Rank2BB;

  Bitboard advance1 = (bPawns >> 8) & (~allPieces);
  Bitboard advance2 = ((advance1 & Rank6BB) >> 8) & (~allPieces) & targets;
  advance1 &= targets;

  Bitboard captureEast = ((bPawns & ~FILE_HBB) >> 7) & pos.pieces(WHITE) & targets;
  Bitboard captureWest = ((bPawns & ~FILE_ABB) >> 9) & pos.pieces(WHITE) & targets;

  while (advance1) {
    Square dest = popLsb(advance1);
    receiver->add(createMove(dest + 8, dest, MT_NORMAL));
  }
  while (advance2) {
    Square dest = popLsb(advance2);
    receiver->add(createMove(dest + 16, dest, MT_NORMAL));
  }
  while (captureEast) {
    Square dest = popLsb(captureEast);
    receiver->add(createMove(dest + 7, dest, MT_NORMAL));
  }
  while (captureWest) {
    Square dest = popLsb(captureWest);
    receiver->add(createMove(dest + 9, dest, MT_NORMAL));
  }

  if (pos.epSquare != SQ_NONE && doEp) {
    Bitboard ourPawnsTakeEp = bPawns & get_pawn_attacks(pos.epSquare, WHITE);

    while (ourPawnsTakeEp) {
      Square src = popLsb(ourPawnsTakeEp);
      receiver->add(createMove(src, pos.epSquare, MT_EN_PASSANT));
    }
  }
}

void getBlackPawnPromotions(const Position& pos, Bitboard targets, MoveList* receiver) {
  const Bitboard allPieces = pos.pieces();
  const Bitboard bPawns = pos.pieces(BLACK, PAWN) & Rank2BB;

  if (bPawns == 0)
    return;

  Bitboard advance1 = (bPawns >> 8) & (~allPieces) & targets;

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

void getWhitePawnPromotions(const Position& pos, Bitboard targets, MoveList* receiver) {
  const Bitboard allPieces = pos.pieces();
  const Bitboard wPawns = pos.pieces(WHITE, PAWN) & Rank7BB;

  if (wPawns == 0)
    return;

  Bitboard advance1 = (wPawns << 8) & (~allPieces) & targets;

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
  const Bitboard allPieces = ourPieces | theirPieces;

  Bitboard targets = ~ourPieces;
  Bitboard kingTargets = targets;
  Bitboard promoTargets = ~ourPieces;

  if (pos.checkers) {
    if (more_than_one(pos.checkers)) {
      addNormalMovesToList(ourKing, get_king_attacks(ourKing) & targets, moveList);
      return;
    }

    targets &= BetweenBB[ourKing][getLsb(pos.checkers)];
    promoTargets &= BetweenBB[ourKing][getLsb(pos.checkers)];
  }

  const Bitboard pinned = pos.blockersForKing[us] & ourPieces;

  if (us == WHITE) {
    getWhitePawnMoves(pos, targets, moveList);
    getWhitePawnPromotions(pos, promoTargets, moveList);
  }
  else {
    getBlackPawnMoves(pos, targets, moveList);
    getBlackPawnPromotions(pos, promoTargets, moveList);
  }

  if (!pos.checkers) {
    if (us == WHITE) {
      if (pos.castlingRights & WHITE_OO) {
        if (!(CASTLING_PATH[WHITE_OO] & allPieces)) {
          moveList->add(createCastlingMove(WHITE_OO));
        }
      }
      if (pos.castlingRights & WHITE_OOO) {
        if (!(CASTLING_PATH[WHITE_OOO] & allPieces)) {
          moveList->add(createCastlingMove(WHITE_OOO));
        }
      }
    }
    else {
      if (pos.castlingRights & BLACK_OO) {
        if (!(CASTLING_PATH[BLACK_OO] & allPieces)) {
          moveList->add(createCastlingMove(BLACK_OO));
        }
      }
      if (pos.castlingRights & BLACK_OOO) {
        if (!(CASTLING_PATH[BLACK_OOO] & allPieces)) {
          moveList->add(createCastlingMove(BLACK_OOO));
        }
      }
    }
  }

  Bitboard knights = ourPieces & pos.pieces(KNIGHT) & ~pinned;
  while (knights) {
    Square from = popLsb(knights);
    addNormalMovesToList(from, get_knight_attacks(from) & targets, moveList);
  }

  Bitboard bishops = ourPieces & pos.pieces(BISHOP, QUEEN);
  while (bishops) {
    Square from = popLsb(bishops);
    Bitboard attacks = get_bishop_attacks(from, allPieces) & targets;
    if (pinned & from)
      attacks &= LineBB[ourKing][from];
    addNormalMovesToList(from, attacks, moveList);
  }

  Bitboard rooks = ourPieces & pos.pieces(ROOK, QUEEN);
  while (rooks) {
    Square from = popLsb(rooks);
    Bitboard attacks = get_rook_attacks(from, allPieces) & targets;
    if (pinned & from)
      attacks &= LineBB[ourKing][from];
    addNormalMovesToList(from, attacks, moveList);
  }

  addNormalMovesToList(ourKing, get_king_attacks(ourKing) & kingTargets, moveList);
}

void getStageMoves(const Position& pos, bool quiets, MoveList* moveList) {
  
  const Color us = pos.sideToMove, them = ~us;

  const Square ourKing = pos.kingSquare(us);

  const Bitboard ourPieces = pos.pieces(us);
  const Bitboard theirPieces = pos.pieces(them);
  const Bitboard allPieces = ourPieces | theirPieces;

  Bitboard targets = quiets ? (~allPieces) : (theirPieces);
  Bitboard kingTargets = targets;
  Bitboard promoTargets = ~ourPieces;
  
  if (pos.checkers) {
    if (more_than_one(pos.checkers)) {
      addNormalMovesToList(ourKing, get_king_attacks(ourKing) & targets, moveList);
      return;
    }

    targets      &= BetweenBB[ourKing][getLsb(pos.checkers)];
    promoTargets &= BetweenBB[ourKing][getLsb(pos.checkers)];
  }

  const Bitboard pinned = pos.blockersForKing[us] & ourPieces;

  if (us == WHITE) {
    getWhitePawnMoves(pos, targets, moveList, !quiets);
    if (!quiets)
      getWhitePawnPromotions(pos, promoTargets, moveList);
  }
  else {
    getBlackPawnMoves(pos, targets, moveList, !quiets);
    if (!quiets)
      getBlackPawnPromotions(pos, promoTargets, moveList);
  }

  if (quiets && !pos.checkers) {
    if (us == WHITE) {
      if (pos.castlingRights & WHITE_OO) {
        if (!(CASTLING_PATH[WHITE_OO] & allPieces)) {
          moveList->add(createCastlingMove(WHITE_OO));
        }
      }
      if (pos.castlingRights & WHITE_OOO) {
        if (!(CASTLING_PATH[WHITE_OOO] & allPieces)) {
          moveList->add(createCastlingMove(WHITE_OOO));
        }
      }
    }
    else {
      if (pos.castlingRights & BLACK_OO) {
        if (!(CASTLING_PATH[BLACK_OO] & allPieces)) {
          moveList->add(createCastlingMove(BLACK_OO));
        }
      }
      if (pos.castlingRights & BLACK_OOO) {
        if (!(CASTLING_PATH[BLACK_OOO] & allPieces)) {
          moveList->add(createCastlingMove(BLACK_OOO));
        }
      }
    }
  }

  Bitboard knights = ourPieces & pos.pieces(KNIGHT) & ~pinned;
  while (knights) {
    Square from = popLsb(knights);
    addNormalMovesToList(from, get_knight_attacks(from) & targets, moveList);
  }

  Bitboard bishops = ourPieces & pos.pieces(BISHOP, QUEEN);
  while (bishops) {
    Square from = popLsb(bishops);
    Bitboard attacks = get_bishop_attacks(from, allPieces) & targets;
    if (pinned & from)
      attacks &= LineBB[ourKing][from];
    addNormalMovesToList(from, attacks, moveList);
  }

  Bitboard rooks = ourPieces & pos.pieces(ROOK, QUEEN);
  while (rooks) {
    Square from = popLsb(rooks);
    Bitboard attacks = get_rook_attacks(from, allPieces) & targets;
    if (pinned & from)
      attacks &= LineBB[ourKing][from];
    addNormalMovesToList(from, attacks, moveList);
  }

  addNormalMovesToList(ourKing, get_king_attacks(ourKing) & kingTargets, moveList);
}