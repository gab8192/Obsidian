#include "position.h"
#include "move.h"
#include "uci.h"

#include <sstream>

/// <summary>
/// What castling rights can remain, once a piece goes from, or to the given square
/// </summary>
CastlingRights ROOK_SQR_TO_CR[SQUARE_NB];

void positionInit() {
  for (Square sq = SQ_A1; sq <= SQ_H8; ++sq) {
    ROOK_SQR_TO_CR[sq] = CastlingRights(~0);
  }

  ROOK_SQR_TO_CR[SQ_A1] = ~WHITE_OOO;
  ROOK_SQR_TO_CR[SQ_H1] = ~WHITE_OO;
  ROOK_SQR_TO_CR[SQ_A8] = ~BLACK_OOO;
  ROOK_SQR_TO_CR[SQ_H8] = ~BLACK_OO;
}

Bitboard Position::attackersTo(Square s, Bitboard occupied) const {

  return  (get_pawn_attacks(s, BLACK)      & pieces(WHITE, PAWN))
        | (get_pawn_attacks(s, WHITE)      & pieces(BLACK, PAWN))
        | (get_knight_attacks(s)           & pieces(KNIGHT))
        | (get_rook_attacks(s, occupied)   & pieces(ROOK, QUEEN))
        | (get_bishop_attacks(s, occupied) & pieces(BISHOP, QUEEN))
        | (get_king_attacks(s)             & pieces(KING));
}

Bitboard Position::attackersTo(Square square, Color attackerColor, Bitboard occupied) const {
  Bitboard attackers;

  attackers  = get_knight_attacks(square)               &  pieces(KNIGHT);
  attackers |= get_king_attacks(square)                 &  pieces(KING);
  attackers |= get_bishop_attacks(square, occupied)     &  pieces(BISHOP, QUEEN);
  attackers |= get_rook_attacks(square, occupied)       &  pieces(ROOK, QUEEN);
  attackers |= get_pawn_attacks(square, ~attackerColor) &  pieces(PAWN);

  return attackers & pieces(attackerColor);
}

Bitboard Position::slidingAttackersTo(Square square, Color attackerColor, Bitboard occupied) const {
  Bitboard attackers;

  attackers = get_bishop_attacks(square, occupied) & pieces(BISHOP, QUEEN);
  attackers |= get_rook_attacks(square, occupied) & pieces(ROOK, QUEEN);

  return attackers & pieces(attackerColor);
}

void Position::updatePins(Color us) {
  const Color them = ~us;

  blockersForKing[us] = 0;
  pinners[them] = 0;

  Square ksq = kingSquare(us);

  Bitboard snipers = ((get_rook_attacks(ksq) & pieces(QUEEN, ROOK))
    | (get_bishop_attacks(ksq) & pieces(QUEEN, BISHOP))) & pieces(them);
  Bitboard occupancy = pieces() ^ snipers;

  while (snipers)
  {
    Square sniperSq = popLsb(snipers);

    Bitboard b = BetweenBB[ksq][sniperSq] & occupancy;

    if (BitCount(b) == 1)
    {
      blockersForKing[us] |= b;
      if (b & pieces(us))
        pinners[them] |= sniperSq;
    }
  }
}

void Position::updateKey() {
  uint64_t newKey = 0;

  Bitboard allPieces = pieces();
  while (allPieces) {
    Square sq = popLsb(allPieces);
    Piece pc = board[sq];

    newKey ^= ZobristPsq[pc][sq];
  }

  newKey ^= ZobristCastling[castlingRights];

  if (epSquare != SQ_NONE)
    newKey ^= ZobristEp[file_of(epSquare)];

  if (sideToMove == WHITE)
    newKey ^= ZobristTempo;

  key = newKey;
}

bool Position::isPseudoLegal(Move move) const {
  if (move == MOVE_NONE)
    return false;

  const Color us = sideToMove, them = ~us;
  const MoveType moveType = move_type(move);
  const Bitboard allPieces = pieces();
  const Square from = move_from(move), to = move_to(move);
  const Piece pc = board[from];

  if (pc == NO_PIECE || colorOf(pc) != us)
    return false;

  if (more_than_one(checkers))
    return ptypeOf(pc) == KING && (get_king_attacks(from) & to & ~pieces(us));

  if (moveType == MT_CASTLING) {
    CastlingRights ct = castling_type(move);
    return
             !checkers
          && (castlingRights & ct)
          && !(CASTLING_PATH[ct] & allPieces);
  }
  else if (moveType == MT_EN_PASSANT) {
    return 
             epSquare != SQ_NONE
          && ptypeOf(pc) == PAWN
          && (get_pawn_attacks(epSquare, them) & from);
  }

  Bitboard targets = ~pieces(us);
  if (ptypeOf(pc) != KING) {
    if (checkers)
      targets &= BetweenBB[kingSquare(us)][getLsb(checkers)];
    if (blockersForKing[us] & from)
      targets &= LineBB[kingSquare(us)][from];
  }

  if (!(targets & to))
    return false;

  if (ptypeOf(pc) == PAWN) {

    const Bitboard sqBB = square_bb(from);
    Bitboard legalTo;

    if (us == WHITE) {
      legalTo = (sqBB << 8) & ~allPieces;
      legalTo |= ((legalTo & Rank3BB) << 8) & ~allPieces;
      legalTo |= (((sqBB & ~FILE_HBB) << 9) | ((sqBB & ~FILE_ABB) << 7)) & pieces(BLACK);
    }
    else {
      legalTo = (sqBB >> 8) & ~allPieces;
      legalTo |= ((legalTo & Rank6BB) >> 8) & ~allPieces;
      legalTo |= (((sqBB & ~FILE_HBB) >> 7) | ((sqBB & ~FILE_ABB) >> 9)) & pieces(WHITE);
    }
    if (moveType != MT_PROMOTION)
      legalTo &= ~(Rank1BB | Rank8BB);

    return legalTo & to;
  }

  return get_piece_attacks(pc, from, pieces()) & to;
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

  if (ptypeOf(movedPc) == KING)
    return !attackersTo(to, ~sideToMove, pieces() ^ from);

  if (!checkers) {
    if (LineBB[from][to] & kingSquare(sideToMove))
      return true;
  }

  if (move_type(move) == MT_EN_PASSANT) {
    Square capSq = (sideToMove == WHITE ? epSquare - 8 : epSquare + 8);
    return !slidingAttackersTo(kingSquare(sideToMove), ~sideToMove, pieces() ^ from ^ capSq ^ to);
  }

  if (ptypeOf(movedPc) == PAWN)
    return !(blockersForKing[sideToMove] & from);

  return true;
}

void Position::doNullMove() {

  const Color us = sideToMove, them = ~us;

  if (epSquare != SQ_NONE) {
    key ^= ZobristEp[file_of(epSquare)];
    epSquare = SQ_NONE;
  }

  gamePly++;

  halfMoveClock++;

  sideToMove = them;
  key ^= ZobristTempo;

  updateAttacksToKings();
}

void Position::doMove(Move move, DirtyPieces& dp) {

  const Color us = sideToMove, them = ~us;

  if (epSquare != SQ_NONE) {
    key ^= ZobristEp[file_of(epSquare)];
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

      if (ptypeOf(capturedPc) == ROOK)
        newCastlingRights &= ROOK_SQR_TO_CR[to];
    }

    movePiece(from, to, movedPc);
    dp.sub0 = {from, movedPc};
    dp.add0 = {to, movedPc};

    switch (ptypeOf(movedPc)) {
    case PAWN: {
      halfMoveClock = 0;

      if (to == from + 16) { // black can take en passant
        epSquare = from + 8;
        key ^= ZobristEp[file_of(epSquare)];
      }
      else if (to == from - 16) { // white can take en passant
        epSquare = from - 8;
        key ^= ZobristEp[file_of(epSquare)];
      }
      break;
    }
    case ROOK: {
      newCastlingRights &= ROOK_SQR_TO_CR[from];
      break;
    }
    case KING: {
      if (us == WHITE) newCastlingRights &= ~WHITE_CASTLING;
      else             newCastlingRights &= ~BLACK_CASTLING;
      break;
    }
    }

    break;
  }
  case MT_CASTLING: {
    const CastlingData* cd = &CASTLING_DATA[castling_type(move)];

    const Square kingSrc = cd->kingSrc, kingDest = cd->kingDest,
                 rookSrc = cd->rookSrc, rookDest = cd->rookDest;

    const Piece ourKingPc = make_piece(us, KING);
    const Piece ourRookPc = make_piece(us, ROOK);

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

    const Piece ourPawnPc = make_piece(us, PAWN);
    const Piece theirPawnPc = make_piece(them, PAWN);
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
    const Piece promoteToPc = make_piece(us, promo_type(move));

    dp.type = capturedPc ? DirtyPieces::CAPTURE : DirtyPieces::NORMAL;

    if (capturedPc != NO_PIECE) {
      removePiece(to, capturedPc);
      dp.sub1 = {to, capturedPc};
      
      if (ptypeOf(capturedPc) == ROOK)
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
  key ^= ZobristTempo;

  updateAttacksToKings();

  if (newCastlingRights != castlingRights) {
    key ^= ZobristCastling[castlingRights ^ newCastlingRights];
    castlingRights = newCastlingRights;
  }
}

/// Only works for MT_NORMAL moves
Key Position::keyAfter(Move move) const {

  Key newKey = key;

  const Color us = sideToMove, them = ~us;

  if (epSquare != SQ_NONE)
    newKey ^= ZobristEp[file_of(epSquare)];

  CastlingRights newCastlingRights = castlingRights;

  const Square from = move_from(move);
  const Square to = move_to(move);

  const Piece movedPc = board[from];
  const Piece capturedPc = board[to];

  if (capturedPc != NO_PIECE) {
    newKey ^= ZobristPsq[capturedPc][to];

    if (ptypeOf(capturedPc) == ROOK)
      newCastlingRights &= ROOK_SQR_TO_CR[to];
  }

  newKey ^= ZobristPsq[movedPc][from] ^ ZobristPsq[movedPc][to];

  switch (ptypeOf(movedPc)) {
  case PAWN: {
    if (to == from + 16 || to == from - 16)
      newKey ^= ZobristEp[file_of(from)];
    break;
  }
  case ROOK: {
    newCastlingRights &= ROOK_SQR_TO_CR[from];
    break;
  }
  case KING: {
    if (us == WHITE) newCastlingRights &= ~WHITE_CASTLING;
    else             newCastlingRights &= ~BLACK_CASTLING;
    break;
  }
  }

  newKey ^= ZobristTempo;

  newKey ^= ZobristCastling[castlingRights ^ newCastlingRights];

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

void Position::setToFen(const std::string& fen, NNUE::Accumulator& acc) {

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
        byPieceBB[ptypeOf(pc)] |= square;
        byColorBB[colorOf(pc)] |= square;
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
    epSquare = make_square(epFile, epRank);

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

  updateAttacksToKings();
  updateKey();
  updateAccumulator(acc);
}

std::string Position::toFenString() const {
  std::ostringstream ss;

  for (Rank r = RANK_8; r >= RANK_1; --r) {
    int emptyC = 0;
    for (File f = FILE_A; f <= FILE_H; ++f) {
      Piece pc = board[make_square(f, r)];
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
      ss << " | " << piecesChar[pos.board[make_square(file, rank)]];
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

bool Position::see_ge(Move m, int threshold) const {

  if (move_type(m) != MT_NORMAL)
    return DRAW >= threshold;

  Square from = move_from(m), to = move_to(m);

  int swap = PieceValue[board[to]] - threshold;
  if (swap < 0)
    return false;

  swap = PieceValue[board[from]] - swap;
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
      if ((swap = PieceValue[PAWN] - swap) < res)
        break;
      occupied ^= getLsb_bb(bb);

      attackers |= get_bishop_attacks(to, occupied) & pieces(BISHOP, QUEEN);
    }

    else if ((bb = stmAttackers & pieces(KNIGHT)))
    {
      if ((swap = PieceValue[KNIGHT] - swap) < res)
        break;
      occupied ^= getLsb_bb(bb);
    }

    else if ((bb = stmAttackers & pieces(BISHOP)))
    {
      if ((swap = PieceValue[BISHOP] - swap) < res)
        break;
      occupied ^= getLsb_bb(bb);

      attackers |= get_bishop_attacks(to, occupied) & pieces(BISHOP, QUEEN);
    }

    else if ((bb = stmAttackers & pieces(ROOK)))
    {
      if ((swap = PieceValue[ROOK] - swap) < res)
        break;
      occupied ^= getLsb_bb(bb);

      attackers |= get_rook_attacks(to, occupied) & pieces(ROOK, QUEEN);
    }

    else if ((bb = stmAttackers & pieces(QUEEN)))
    {
      if ((swap = PieceValue[QUEEN] - swap) < res)
        break;
      occupied ^= getLsb_bb(bb);

      attackers |= (get_bishop_attacks(to, occupied) & pieces(BISHOP, QUEEN))
                 | (get_rook_attacks(to, occupied) & pieces(ROOK, QUEEN));
    }
    else 
      return (attackers & ~pieces(stm)) ? res ^ 1 : res;
  }

  return bool(res);
}

void Position::updateAccumulator(NNUE::Accumulator& acc) const {
  acc.reset();

  Bitboard b0 = pieces();
  while (b0) {
    Square sq = popLsb(b0);
    acc.activateFeature(sq, board[sq], &acc);
  }
}