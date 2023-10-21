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

    newKey ^= RANDOM_ARRAY[64 * HASH_PIECE[pc] + sq];
  }

  newKey ^= HASH_CASTLING[castlingRights];

  if (epSquare != SQ_NONE)
    newKey ^= RANDOM_ARRAY[772 + file_of(epSquare)];

  if (sideToMove == WHITE)
    newKey ^= RANDOM_ARRAY[780];

  key = newKey;
}

bool Position::isLegal(Move move) {
  if (checkers) {
    const Square from = getMoveSrc(move);
    const Square to = getMoveDest(move);

    const Piece movedPc = board[from];

    if (ptypeOf(movedPc) == KING)
      return !attackersTo(to, ~sideToMove, pieces() ^ from);

    if (getMoveType(move) == MT_EN_PASSANT) {
      Square capSq = (sideToMove == WHITE ? epSquare - 8 : epSquare + 8);
      return !slidingAttackersTo(kingSquare(sideToMove), ~sideToMove, pieces() ^ from ^ capSq ^ to);
    }

    if (ptypeOf(movedPc) == PAWN)
      return ! (blockersForKing[sideToMove] & from);

    return true;
  }
  else {

    if (getMoveType(move) == MT_CASTLING) {
      switch (getCastlingType(move)) 
      {
      case WHITE_OO: return !attackersTo(SQ_F1, BLACK) && !attackersTo(SQ_G1, BLACK);
      case WHITE_OOO: return !attackersTo(SQ_D1, BLACK) && !attackersTo(SQ_C1, BLACK);
      case BLACK_OO: return !attackersTo(SQ_F8, WHITE) && !attackersTo(SQ_G8, WHITE);
      case BLACK_OOO: return !attackersTo(SQ_D8, WHITE) && !attackersTo(SQ_C8, WHITE);
      }
    }

    const Square from = getMoveSrc(move);
    const Square to = getMoveDest(move);

    const Piece movedPc = board[from];

    if (ptypeOf(movedPc) == KING)
      return !attackersTo(to, ~sideToMove, pieces() ^ from);

    if (LineBB[from][to] & kingSquare(sideToMove))
      return true;

    if (getMoveType(move) == MT_EN_PASSANT) {
      Square capSq = (sideToMove == WHITE ? epSquare - 8 : epSquare + 8);
      return !slidingAttackersTo(kingSquare(sideToMove), ~sideToMove, pieces() ^ from ^ capSq ^ to);
    }

    if (ptypeOf(movedPc) == PAWN)
      return !(blockersForKing[sideToMove] & from);

    return true;
  }
}

void Position::doNullMove() {

  const Color us = sideToMove, them = ~us;

  if (epSquare != SQ_NONE) {
    key ^= RANDOM_ARRAY[772 + file_of(epSquare)];
    epSquare = SQ_NONE;
  }

  gamePly++;

  halfMoveClock++;

  sideToMove = them;
  key ^= RANDOM_ARRAY[780];

  updateAttacksToKings();
}

void Position::doMove(Move move, NNUE::Accumulator* acc) {

  const Color us = sideToMove, them = ~us;

  if (epSquare != SQ_NONE) {
    key ^= RANDOM_ARRAY[772 + file_of(epSquare)];
    epSquare = SQ_NONE;
  }

  gamePly++;

  halfMoveClock++;

  CastlingRights oldCastlingRights = castlingRights;

  const MoveType moveType = getMoveType(move);

  switch (moveType) {
  case MT_NORMAL: {
    const Square from = getMoveSrc(move);
    const Square to = getMoveDest(move);

    const Piece movedPc = board[from];
    const Piece capturedPc = board[to];

    if (capturedPc != NO_PIECE) {
      halfMoveClock = 0;

      removePiece(to, capturedPc, acc);

      if (ptypeOf(capturedPc) == ROOK)
        castlingRights &= ROOK_SQR_TO_CR[to];
    }

    movePiece(from, to, movedPc, acc);

    switch (ptypeOf(movedPc)) {
    case PAWN: {
      halfMoveClock = 0;

      if (to == from + 16) { // black can take en passant
        epSquare = from + 8;
        key ^= RANDOM_ARRAY[772 + file_of(epSquare)];
      }
      else if (to == from - 16) { // white can take en passant
        epSquare = from - 8;
        key ^= RANDOM_ARRAY[772 + file_of(epSquare)];
      }
      break;
    }
    case ROOK: {
      castlingRights &= ROOK_SQR_TO_CR[from];
      break;
    }
    case KING: {
      if (us == WHITE) castlingRights &= ~WHITE_CASTLING;
      else             castlingRights &= ~BLACK_CASTLING;
      break;
    }
    }

    break;
  }
  case MT_CASTLING: {
    const CastlingData* cd = &CASTLING_DATA[getCastlingType(move)];

    const Square kingSrc = cd->kingSrc, kingDest = cd->kingDest,
                 rookSrc = cd->rookSrc, rookDest = cd->rookDest;

    if (us == WHITE) {
      movePiece(kingSrc, kingDest, W_KING, acc);
      movePiece(rookSrc, rookDest, W_ROOK, acc);

      castlingRights &= ~WHITE_CASTLING;
    }
    else {
      movePiece(kingSrc, kingDest, B_KING, acc);
      movePiece(rookSrc, rookDest, B_ROOK, acc);

      castlingRights &= ~BLACK_CASTLING;
    }

    break;
  }
  case MT_EN_PASSANT: {
    halfMoveClock = 0;

    const Square from = getMoveSrc(move);
    const Square to = getMoveDest(move);

    if (us == WHITE) {

      removePiece(to - 8, B_PAWN, acc);
      movePiece(from, to, W_PAWN, acc);
    }
    else {

      removePiece(to + 8, W_PAWN, acc);
      movePiece(from, to, B_PAWN, acc);
    }

    break;
  }
  case MT_PROMOTION: {
    halfMoveClock = 0;

    const Square from = getMoveSrc(move);
    const Square to = getMoveDest(move);

    const Piece capturedPc = board[to];
    const Piece promoteToPc = make_piece(us, getPromoType(move));

    if (capturedPc != NO_PIECE) {
      removePiece(to, capturedPc, acc);
      if (ptypeOf(capturedPc) == ROOK)
        castlingRights &= ROOK_SQR_TO_CR[to];
    }

    removePiece(from, board[from], acc);
    putPiece(to, promoteToPc, acc);

    break;
  }
  }

  sideToMove = them;
  key ^= RANDOM_ARRAY[780];

  updateAttacksToKings();

  if (castlingRights != oldCastlingRights)
    key ^= HASH_CASTLING[oldCastlingRights] ^ HASH_CASTLING[castlingRights];
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

void Position::setToFen(const string& fen, NNUE::Accumulator* accumulator) {

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
  updateAccumulator(accumulator);
}

string Position::toFenString() const {
  ostringstream ss;

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
    ss << ' ' << UCI::square(epSquare) << ' ';

  ss << halfMoveClock << ' ';

  ss << (1 + (gamePly - (sideToMove == BLACK)) / 2);

  return ss.str();
}

std::ostream& operator<<(std::ostream& stream, Position& pos) {

  const string rowSeparator = "\n +---+---+---+---+---+---+---+---+";

  ostringstream ss;
  
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

bool Position::see_ge(Move m, Value threshold) const {

  // Only deal with normal moves, assume others pass a simple SEE
  if (getMoveType(m) != MT_NORMAL)
    return VALUE_DRAW >= threshold;

  Square from = getMoveSrc(m), to = getMoveDest(m);

  int swap = PieceValue[board[to]] - threshold;
  if (swap < 0)
    return false;

  swap = PieceValue[board[from]] - swap;
  if (swap <= 0)
    return true;

  //assert(color_of(board[from]) == sideToMove);

  Bitboard occupied = pieces() ^ from ^ to; // xoring to is important for pinned piece logic
  Color stm = sideToMove;
  Bitboard attackers = attackersTo(to, occupied);
  Bitboard stmAttackers, bb;
  int res = 1;

  while (true) {

    stm = ~stm;
    attackers &= occupied;

    // If stm has no more attackers then give up: stm loses
    if (!(stmAttackers = attackers & pieces(stm)))
      break;

    // Don't allow pinned pieces to attack as long as there are
    // pinners on their original square.
    if (pinners[~stm] & occupied) {
      stmAttackers &= ~blockersForKing[stm];

      if (!stmAttackers)
        break;
    }

    res ^= 1;

    // Locate and remove the next least valuable attacker, and add to
    // the bitboard 'attackers' any X-ray attackers behind it.
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

    else // KING
         // If we "capture" with the king but the opponent still has attackers,
         // reverse the result.
      return (attackers & ~pieces(stm)) ? res ^ 1 : res;
  }

  return bool(res);
}

void Position::updateAccumulator(NNUE::Accumulator* acc) {
  acc->reset();

  Bitboard b0 = pieces();
  while (b0) {
    Square sq = popLsb(b0);
    acc->activateFeature(sq, board[sq]);
  }
}