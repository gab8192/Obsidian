#pragma once

#include "bitboard.h"
#include "move.h"
#include "nnue.h"
#include "types.h"
#include "zobrist.h"

/// <summary>
/// Called once at engine initialization
/// </summary>
void positionInit();

struct Threats {
  Bitboard byPawn;
  Bitboard byMinor;
  Bitboard byRook;
};

struct alignas(32) Position {
  Color sideToMove;
  Square epSquare;
  CastlingRights castlingRights;

  Bitboard byColorBB[COLOR_NB];
  Bitboard byPieceBB[PIECE_TYPE_NB];
  Piece board[SQUARE_NB];

  int halfMoveClock;
  int gamePly;

  Key key;
  Key pawnKey;

  Bitboard blockersForKing[COLOR_NB];
  Bitboard pinners[COLOR_NB];

  // What pieces of the opponent are attacking the king of the side to move
  Bitboard checkers;

  inline Bitboard pieces(PieceType pt) const {
    return byPieceBB[pt];
  }

  inline Bitboard pieces(Color c) const {
    return byColorBB[c];
  }

  inline Bitboard pieces(Color c, PieceType pt) const {
    return byColorBB[c] & byPieceBB[pt];
  }

  inline Bitboard pieces(PieceType pt0, PieceType pt1) const {
    return byPieceBB[pt0] | byPieceBB[pt1];
  }

  inline Bitboard pieces() const {
    return byColorBB[WHITE] | byColorBB[BLACK];
  }

  inline Square kingSquare(Color c) const {
    return getLsb(pieces(c, KING));
  }

  inline CastlingRights castlingRightsOf(Color c) const {

    if (c == WHITE)
      return WHITE_CASTLING & castlingRights;

    return BLACK_CASTLING & castlingRights;
  }

  inline bool hasNonPawns(Color c) const {
    return pieces(c) & ~pieces(PAWN, KING);
  }

  Bitboard attackersTo(Square square, Bitboard occupied) const;
  Bitboard attackersTo(Square square, Color attackerColor, Bitboard occupied) const;
  Bitboard slidingAttackersTo(Square square, Color attackerColor, Bitboard occupied) const;

  inline Bitboard attackersTo(Square square, Color attackerColor) const {
    return attackersTo(square, attackerColor, pieces());
  }

  void updatePins(Color color);

  /// <summary>
  /// Invoke AFTER the side to move has been updated.
  /// Refreshes blockersForKing, pinners, checkers, threats
  /// </summary>
  inline void updateAttacks() {
    updatePins(WHITE);
    updatePins(BLACK);

    checkers = attackersTo(kingSquare(sideToMove), ~sideToMove);
  }

  void updateKeys();

  /// <summary>
  /// Assmue there is a piece in the given square.
  /// Call this if you already know what piece was there
  /// </summary>
  inline void removePiece(Square sq, Piece pc) {
    key ^= ZOBRIST_PSQ[pc][sq];

    if(piece_type(pc) == PAWN)
      pawnKey ^= ZOBRIST_PSQ[pc][sq];

    board[sq] = NO_PIECE;
    byColorBB[piece_color(pc)] ^= sq;
    byPieceBB[piece_type(pc)] ^= sq;


  }

  /// <summary>
  /// Assmue there is not any piece in the given square
  /// </summary>
  inline void putPiece(Square sq, Piece pc) {
    key ^= ZOBRIST_PSQ[pc][sq];

    if(piece_type(pc) == PAWN)
      pawnKey ^= ZOBRIST_PSQ[pc][sq];

    board[sq] = pc;
    byColorBB[piece_color(pc)] ^= sq;
    byPieceBB[piece_type(pc)] ^= sq;
  }

  /// <summary>
  /// Assmue there is not any piece in the destination square
  /// Call this if you already know what piece was there
  /// </summary>
  inline void movePiece(Square from, Square to, Piece pc) {

    key ^= ZOBRIST_PSQ[pc][from] ^ ZOBRIST_PSQ[pc][to];

  if(piece_type(pc) == PAWN)
      pawnKey ^= ZOBRIST_PSQ[pc][from] ^ ZOBRIST_PSQ[pc][to];

    board[from] = NO_PIECE;
    board[to] = pc;
    const Bitboard fromTo = from | to;
    byColorBB[piece_color(pc)] ^= fromTo;
    byPieceBB[piece_type(pc)] ^= fromTo;
  }

  inline bool isQuiet(Move move) const {
    MoveType mt = move_type(move);

    return    (mt == MT_NORMAL && ! board[move_to(move)]) 
           || (mt == MT_CASTLING);
  }

  bool isPseudoLegal(Move move) const;

  bool isLegal(Move move) const;

  void doNullMove();

  void doMove(Move move, DirtyPieces& dp);

  void calcThreats(Threats& threats);

  /// Only works for MT_NORMAL moves
  Key keyAfter(Move move) const;

  bool seeGe(Move m, int threshold) const;

  void setToFen(const std::string& fen);

  std::string toFenString() const;
};

std::ostream& operator<<(std::ostream& stream, Position& sqr);