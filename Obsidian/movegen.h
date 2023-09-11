#pragma once

#include "bitboard.h"
#include "move.h"
#include "position.h"
#include "types.h"

/*
* All legal moves, plus ones that could make the side to move fall in check
*/
void getPseudoLegalMoves(const Position& pos, MoveList* moveList);

/*
* All legal captures, plus ones that could make the side to move fall in check
*/
void getAggressiveMoves(const Position& pos, MoveList* moveList);