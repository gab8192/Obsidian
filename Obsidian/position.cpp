#include "position.h"
#include "move.h"
#include "uci.h"

#include <sstream>

/// <summary>
/// What castling rights can remain, once a piece goes from, or to the given square
/// </summary>
CastlingRights ROOK_SQR_TO_CR[SQUARE_NB];

void positionInit() {
  for (Square sq = SQ_A1; sq <= SQ_H8; ++sq)
    ROOK_SQR_TO_CR[sq] = ALL_CASTLING;

  ROOK_SQR_TO_CR[SQ_A1] = ~WHITE_OOO;
  ROOK_SQR_TO_CR[SQ_H1] = ~WHITE_OO;
  ROOK_SQR_TO_CR[SQ_A8] = ~BLACK_OOO;
  ROOK_SQR_TO_CR[SQ_H8] = ~BLACK_OO;
}

Bitboard Position::attackersTo(Square s, Bitboard occupied) const {

  return  (getPawnAttacks(s, BLACK)      & pieces(WHITE, PAWN))
        | (getPawnAttacks(s, WHITE)      & pieces(BLACK, PAWN))
        | (getKnightAttacks(s)           & pieces(KNIGHT))
        | (getRookAttacks(s, occupied)   & pieces(ROOK, QUEEN))
        | (getBishopAttacks(s, occupied) & pieces(BISHOP, QUEEN))
        | (getKingAttacks(s)             & pieces(KING));
}

Bitboard Position::attackersTo(Square square, Color attackerColor, Bitboard occupied) const {
  Bitboard attackers;

  attackers  = getKnightAttacks(square)               &  pieces(KNIGHT);
  attackers |= getKingAttacks(square)                 &  pieces(KING);
  attackers |= getBishopAttacks(square, occupied)     &  pieces(BISHOP, QUEEN);
  attackers |= getRookAttacks(square, occupied)       &  pieces(ROOK, QUEEN);
  attackers |= getPawnAttacks(square, ~attackerColor) &  pieces(PAWN);

  return attackers & pieces(attackerColor);
}

Bitboard Position::slidingAttackersTo(Square square, Color attackerColor, Bitboard occupied) const {
  Bitboard attackers;

  attackers = getBishopAttacks(square, occupied) & pieces(BISHOP, QUEEN);
  attackers |= getRookAttacks(square, occupied) & pieces(ROOK, QUEEN);

  return attackers & pieces(attackerColor);
}

void Position::updatePins(Color us) {
  const Color them = ~us;

  blockersForKing[us] = 0;
  pinners[them] = 0;

  Square ksq = kingSquare(us);

  Bitboard snipers = ((getRookAttacks(ksq) & pieces(QUEEN, ROOK))
    | (getBishopAttacks(ksq) & pieces(QUEEN, BISHOP))) & pieces(them);
  Bitboard occupied = pieces() ^ snipers;

  while (snipers)
  {
    Square sniperSq = popLsb(snipers);

    Bitboard b = BETWEEN_BB[ksq][sniperSq] & occupied;

    if (BitCount(b) == 1)
    {
      blockersForKing[us] |= b;
      if (b & pieces(us))
        pinners[them] |= sniperSq;
    }
  }
}

void Position::updateKeys() {
  uint64_t newKey = 0;
  uint64_t newPawnKey = 0;

  Bitboard allPieces = pieces();
  while (allPieces) {
    Square sq = popLsb(allPieces);
    Piece pc = board[sq];

    newKey ^= ZOBRIST_PSQ[pc][sq];

    if(piece_type(pc) == PAWN)
      newPawnKey ^= ZOBRIST_PSQ[pc][sq];
    
  }

  newKey ^= ZOBRIST_CASTLING[castlingRights];

  if (epSquare != SQ_NONE)
    newKey ^= ZOBRIST_EP[fileOf(epSquare)];

  if (sideToMove == WHITE)
    newKey ^= ZOBRIST_TEMPO;

  key = newKey;
}

bool Position::isPseudoLegal(Move move) const {
  if (move == MOVE_NONE)
    return false;

  const Color us = sideToMove, them = ~us;
  const MoveType moveType = move_type(move);
  const Bitboard occupied = pieces();
  const Bitboard emptySquares = ~occupied;
  const Square from = move_from(move), to = move_to(move);
  const Piece pc = board[from];

  if (pc == NO_PIECE || piece_color(pc) != us)
    return false;

  if (pieces(us) & to)
    return false;

  if (moreThanOne(checkers))
    return moveType == MT_NORMAL && piece_type(pc) == KING && (getKingAttacks(from) & to);

  if (moveType == MT_CASTLING) {
    CastlingRights ct = castling_type(move);
    return
             !checkers
          && (castlingRights & ct)
          && !(CASTLING_PATH[ct] & occupied);
  }

  if (moveType == MT_EN_PASSANT) {
    return 
             to == epSquare /* implies epSquare != SQ_NONE */
          && piece_type(pc) == PAWN
          && (getPawnAttacks(epSquare, them) & from);
  }

  if (moveType == MT_PROMOTION && piece_type(pc) != PAWN)
    return false;

  if (piece_type(pc) == KING)
    return getKingAttacks(from) & to;

  if (checkers)
    if (! (BETWEEN_BB[kingSquare(us)][getLsb(checkers)] & to))
      return false;

  if (piece_type(pc) == PAWN) {

    const Bitboard sqBB = squareBB(from);
    Bitboard legalTo;

    if (us == WHITE) {
      legalTo = (sqBB << 8) & emptySquares;
      legalTo |= ((legalTo & Rank3BB) << 8) & emptySquares;
      legalTo |= (((sqBB & ~FILE_HBB) << 9) | ((sqBB & ~FILE_ABB) << 7)) & pieces(BLACK);
    }
    else {
      legalTo = (sqBB >> 8) & emptySquares;
      legalTo |= ((legalTo & Rank6BB) >> 8) & emptySquares;
      legalTo |= (((sqBB & ~FILE_HBB) >> 7) | ((sqBB & ~FILE_ABB) >> 9)) & pieces(WHITE);
    }
    if (moveType != MT_PROMOTION)
      legalTo &= ~(Rank1BB | Rank8BB);

    return legalTo & to;
  }

  if ((blockersForKing[us] & from))
    if (! (LINE_BB[kingSquare(us)][from] & to))
      return false;

  return getPieceAttacks(piece_type(pc), from, pieces()) & to;
}

bool Position::isLegal(Move move) const {

  if (move_type(move) == MT_CASTLING) {
    switch (castling_type(move))
    {
    case WHITE_OO: return !attackersTo(SQ_F1, BLACK) && !attackersTo(SQ_G1, BLACK);
    case WHITE_OOO: return !attackersTo(SQ_D1, BLACK) && !attackersTo(SQ_C1, BLACK);
    case BLACK_OO: return !attackersTo(SQ_F8, WHITE) && !attackersTo(SQ_G8, WHITE);
    case BLACK_OOO: return !attackersTo(SQ_D8, WHITE) && !attackersTo(SQ_C8, WHITE);
    }
  }

  const Square from = move_from(move);
  const Square to = move_to(move);
  const Piece movedPc = board[from];

  if (piece_type(movedPc) == KING)
    return !attackersTo(to, ~sideToMove, pieces() ^ from);

  if (!checkers) {
    if (LINE_BB[from][to] & kingSquare(sideToMove))
      return true;
  }

  if (move_type(move) == MT_EN_PASSANT) {
    Square capSq = (sideToMove == WHITE ? epSquare - 8 : epSquare + 8);
    return !slidingAttackersTo(kingSquare(sideToMove), ~sideToMove, pieces() ^ from ^ capSq ^ to);
  }

  if (piece_type(movedPc) == PAWN)
    return !(blockersForKing[sideToMove] & from);

  return true;
}

void Position::doNullMove() {

  const Color us = sideToMove, them = ~us;

  if (epSquare != SQ_NONE) {
    key ^= ZOBRIST_EP[fileOf(epSquare)];
    epSquare = SQ_NONE;
  }

  gamePly++;

  halfMoveClock++;

  sideToMove = them;
  key ^= ZOBRIST_TEMPO;

  updateAttacks();
}

void Position::doMove(Move move, DirtyPieces& dp) {

  const Color us = sideToMove, them = ~us;

  if (epSquare != SQ_NONE) {
    key ^= ZOBRIST_EP[fileOf(epSquare)];
    epSquare = SQ_NONE;
  }

  gamePly++;

  halfMoveClock++;

  CastlingRights newCastlingRights = castlingRights;

  const MoveType moveType = move_type(move);

  switch (moveType) {
  case MT_NORMAL: {
    const Square from = move_from(move);
    const Square to = move_to(move);

    const Piece movedPc = board[from];
    const Piece capturedPc = board[to];

    dp.type = capturedPc ? DirtyPieces::CAPTURE : DirtyPieces::NORMAL;

    if (capturedPc != NO_PIECE) {
      halfMoveClock = 0;

      removePiece(to, capturedPc);
      dp.sub1 = {to, capturedPc};

      if (piece_type(capturedPc) == ROOK)
        newCastlingRights &= ROOK_SQR_TO_CR[to];
    }

    movePiece(from, to, movedPc);
    dp.sub0 = {from, movedPc};
    dp.add0 = {to, movedPc};

    switch (piece_type(movedPc)) {
    case PAWN: {
      halfMoveClock = 0;

      int push = (us == WHITE ? 8 : -8);

      if (to == from + 2*push) {
        if (getPawnAttacks(from + push, us) & pieces(them, PAWN)) {
          epSquare = from + push;
          key ^= ZOBRIST_EP[fileOf(epSquare)];
        }
      }
      break;
    }
    case ROOK: {
      newCastlingRights &= ROOK_SQR_TO_CR[from];
      break;
    }
    case KING: {
      newCastlingRights &= (us == WHITE ? BLACK_CASTLING : WHITE_CASTLING);
      break;
    }
    }

    break;
  }
  case MT_CASTLING: {
    const CastlingData* cd = &CASTLING_DATA[castling_type(move)];

    const Square kingSrc = cd->kingSrc, kingDest = cd->kingDest,
                 rookSrc = cd->rookSrc, rookDest = cd->rookDest;

    const Piece ourKingPc = makePiece(us, KING);
    const Piece ourRookPc = makePiece(us, ROOK);

    movePiece(kingSrc, kingDest, ourKingPc);
    movePiece(rookSrc, rookDest, ourRookPc);
    newCastlingRights &= (us == WHITE ? BLACK_CASTLING : WHITE_CASTLING);

    dp.type = DirtyPieces::CASTLING;
    dp.sub0 = {kingSrc, ourKingPc};
    dp.add0 = {kingDest, ourKingPc};
    dp.sub1 = {rookSrc, ourRookPc};
    dp.add1 = {rookDest, ourRookPc};

    break;
  }
  case MT_EN_PASSANT: {
    halfMoveClock = 0;

    const Square from = move_from(move);
    const Square to = move_to(move);

    const Piece ourPawnPc = makePiece(us, PAWN);
    const Piece theirPawnPc = makePiece(them, PAWN);
    const Square capSq = (us == WHITE ? to-8 : to+8);

    removePiece(capSq, theirPawnPc);
    movePiece(from, to, ourPawnPc);
    
    dp.type = DirtyPieces::CAPTURE;
    dp.sub1 = {capSq, theirPawnPc};
    dp.sub0 = {from, ourPawnPc};
    dp.add0 = {to, ourPawnPc};

    break;
  }
  case MT_PROMOTION: {
    halfMoveClock = 0;

    const Square from = move_from(move);
    const Square to = move_to(move);

    const Piece movedPc = board[from];
    const Piece capturedPc = board[to];
    const Piece promoteToPc = makePiece(us, promo_type(move));

    dp.type = capturedPc ? DirtyPieces::CAPTURE : DirtyPieces::NORMAL;

    if (capturedPc != NO_PIECE) {
      removePiece(to, capturedPc);
      dp.sub1 = {to, capturedPc};
      
      if (piece_type(capturedPc) == ROOK)
        newCastlingRights &= ROOK_SQR_TO_CR[to];
    }

    removePiece(from, movedPc);
    putPiece(to, promoteToPc);
    dp.sub0 = {from, movedPc};
    dp.add0 = {to, promoteToPc};
    
    break;
  }
  }

  sideToMove = them;
  key ^= ZOBRIST_TEMPO;

  updateAttacks();

  if (newCastlingRights != castlingRights) {
    key ^= ZOBRIST_CASTLING[castlingRights ^ newCastlingRights];
    castlingRights = newCastlingRights;
  }
}

void Position::calcThreats(Threats& threats) {

  Color them = ~sideToMove;

  threats.byPawn = getPawnBbAttacks(pieces(them, PAWN), them);
  threats.byMinor = threats.byPawn;
  Bitboard knights = pieces(them, KNIGHT);
  while (knights) {
    Square sq = popLsb(knights);
    threats.byMinor |= getKnightAttacks(sq);
  }
  Bitboard bishops = pieces(them, BISHOP);
  while (bishops) {
    Square sq = popLsb(bishops);
    threats.byMinor |= getBishopAttacks(sq, pieces());
  }
  threats.byRook = threats.byMinor;
  Bitboard rooks = pieces(them, ROOK);
  while (rooks) {
    Square sq = popLsb(rooks);
    threats.byRook |= getRookAttacks(sq, pieces());
  }
}

/// Only works for MT_NORMAL moves
Key Position::keyAfter(Move move) const {

  Key newKey = key;

  const Square from = move_from(move);
  const Square to = move_to(move);

  const Piece movedPc = board[from];
  const Piece capturedPc = board[to];

  if (capturedPc != NO_PIECE)
    newKey ^= ZOBRIST_PSQ[capturedPc][to];

  newKey ^= ZOBRIST_PSQ[movedPc][from] ^ ZOBRIST_PSQ[movedPc][to];

  newKey ^= ZOBRIST_TEMPO;

  return newKey;
}

int readNumberTillSpace(const std::string& str, int& i) {
  int num = 0;
  while (str[i] && str[i] != ' ') {
    num *= 10;
    num += str[i] - '0';
    i++;
  }
  return num;
}

void Position::setToFen(const std::string& fen) {

  memset(this, 0, sizeof(Position));

  int idx = 0;
  {
    char c;
    Square square = SQ_A8;
    while ((c = fen[idx++]) != ' ')
    {
      if (c == '/')
        square -= 16;
      else if (c >= '1' && c <= '8')
        square += c - '0';
      else
      {
        Piece pc = NO_PIECE;
        switch (c)
        {
        case 'p': pc = B_PAWN; break;
        case 'P': pc = W_PAWN; break;
        case 'n': pc = B_KNIGHT; break;
        case 'N': pc = W_KNIGHT; break;
        case 'b': pc = B_BISHOP; break;
        case 'B': pc = W_BISHOP; break;
        case 'r': pc = B_ROOK; break;
        case 'R': pc = W_ROOK; break;
        case 'q': pc = B_QUEEN; break;
        case 'Q': pc = W_QUEEN; break;
        case 'k': pc = B_KING; break;
        case 'K': pc = W_KING; break;
        }
        board[square] = pc;
        byPieceBB[piece_type(pc)] |= square;
        byColorBB[piece_color(pc)] |= square;
        ++square;
      }
    }
  }

  sideToMove = (fen[idx] == 'b') ? BLACK : WHITE;

  idx += 2; // letter and space

  if (fen[idx] != '-') {
    char c;
    
    while ((c = fen[idx++]) != ' ') {
      switch (c) {
      case 'k': castlingRights |= BLACK_OO;  break;
      case 'q': castlingRights |= BLACK_OOO; break;
      case 'K': castlingRights |= WHITE_OO;  break;
      case 'Q': castlingRights |= WHITE_OOO; break;
      }
    }
  }
  else
    idx += 2; // hyphen and space

  if (fen[idx] != '-')
  {

    File epFile = File(fen[idx++] - 'a');
    Rank epRank = Rank(fen[idx++] - '1');      // should always be RANK_2 or RANK_7
    epSquare = makeSquare(epFile, epRank);

    if (! (getPawnAttacks(epSquare, ~sideToMove) & pieces(sideToMove, PAWN)))
      epSquare = SQ_NONE;

    idx++; // space
  }
  else {
    epSquare = SQ_NONE;
    idx += 2; // hyphen and space
  }

  // Accept incomplete FENs
  if (fen.size() > idx) {
    halfMoveClock = readNumberTillSpace(fen, idx);
    idx++;
    gamePly = readNumberTillSpace(fen, idx);
  }
  gamePly = std::max(2 * (gamePly - 1), 0) + (sideToMove == BLACK);

  updateAttacks();
  updateKeys();
}

std::string Position::toFenString() const {
  std::ostringstream ss;

  for (Rank r = RANK_8; r >= RANK_1; --r) {
    int emptyC = 0;
    for (File f = FILE_A; f <= FILE_H; ++f) {
      Piece pc = board[makeSquare(f, r)];
      if (pc == NO_PIECE) {
        emptyC++;
      }
      else {
        if (emptyC)
          ss << emptyC;
        ss << piecesChar[pc];
        emptyC = 0;
      }
    }
    if (emptyC)
      ss << emptyC;

    if (r != RANK_1)
      ss << '/';
  }

  ss << (sideToMove == WHITE ? " w " : " b ");

  if (castlingRights & WHITE_OO)
    ss << 'K';
  if (castlingRights & WHITE_OOO)
    ss << 'Q';
  if (castlingRights & BLACK_OO)
    ss << 'k';
  if (castlingRights & BLACK_OOO)
    ss << 'q';

  if (!castlingRights)
    ss << '-';

  if (epSquare == SQ_NONE)
    ss << " - ";
  else
    ss << ' ' << UCI::squareToString(epSquare) << ' ';

  ss << halfMoveClock << ' ';

  ss << (1 + (gamePly - (sideToMove == BLACK)) / 2);

  return ss.str();
}

std::ostream& operator<<(std::ostream& stream, Position& pos) {

  const std::string rowSeparator = "\n +---+---+---+---+---+---+---+---+";

  std::ostringstream ss;
  
  for (Rank rank = RANK_8; rank >= RANK_1; --rank)
  {
    ss << rowSeparator << '\n';
    for (File file = FILE_A; file <= FILE_H; ++file)
    {
      ss << " | " << piecesChar[pos.board[makeSquare(file, rank)]];
    }
    ss << " | " << char('1' + rank);
  }
  ss << rowSeparator;
  ss << "\n   a   b   c   d   e   f   g   h\n";
  ss << "\nKey: " << (void*) pos.key;
  ss << "\nFEN: " << pos.toFenString();

  stream << ss.str();

  return stream;
}

bool Position::seeGe(Move m, int threshold) const {

  if (move_type(m) != MT_NORMAL)
    return true;

  Square from = move_from(m), to = move_to(m);

  int swap = PIECE_VALUE[board[to]] - threshold;
  if (swap < 0)
    return false;

  swap = PIECE_VALUE[board[from]] - swap;
  if (swap <= 0)
    return true;

  Bitboard occupied = pieces() ^ from ^ to;
  Color stm = sideToMove;
  Bitboard attackers = attackersTo(to, occupied);
  Bitboard stmAttackers, bb;
  int res = 1;

  while (true) {

    stm = ~stm;
    attackers &= occupied;

    if (!(stmAttackers = attackers & pieces(stm)))
      break;

    if (pinners[~stm] & occupied) {
      stmAttackers &= ~blockersForKing[stm];

      if (!stmAttackers)
        break;
    }

    res ^= 1;
    if ((bb = stmAttackers & pieces(PAWN)))
    {
      if ((swap = PIECE_VALUE[PAWN] - swap) < res)
        break;
      occupied ^= getLsb_bb(bb);

      attackers |= getBishopAttacks(to, occupied) & pieces(BISHOP, QUEEN);
    }

    else if ((bb = stmAttackers & pieces(KNIGHT)))
    {
      if ((swap = PIECE_VALUE[KNIGHT] - swap) < res)
        break;
      occupied ^= getLsb_bb(bb);
    }

    else if ((bb = stmAttackers & pieces(BISHOP)))
    {
      if ((swap = PIECE_VALUE[BISHOP] - swap) < res)
        break;
      occupied ^= getLsb_bb(bb);

      attackers |= getBishopAttacks(to, occupied) & pieces(BISHOP, QUEEN);
    }

    else if ((bb = stmAttackers & pieces(ROOK)))
    {
      if ((swap = PIECE_VALUE[ROOK] - swap) < res)
        break;
      occupied ^= getLsb_bb(bb);

      attackers |= getRookAttacks(to, occupied) & pieces(ROOK, QUEEN);
    }

    else if ((bb = stmAttackers & pieces(QUEEN)))
    {
      if ((swap = PIECE_VALUE[QUEEN] - swap) < res)
        break;
      occupied ^= getLsb_bb(bb);

      attackers |= (getBishopAttacks(to, occupied) & pieces(BISHOP, QUEEN))
                 | (getRookAttacks(to, occupied) & pieces(ROOK, QUEEN));
    }
    else 
      return (attackers & ~pieces(stm)) ? res ^ 1 : res;
  }

  return bool(res);
}