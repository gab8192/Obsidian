#pragma once

#include "move.h"
#include "position.h"

enum MoveGenFlags {
    ADD_QUIETS = 1,
    ADD_CAPTURES = 2,
    ADD_ALL_MOVES = ADD_QUIETS | ADD_CAPTURES
};

void getStageMoves(const Position& pos, MoveGenFlags flags, MoveList* moveList);

/// @brief Do not invoke when in check
void getQuietChecks(const Position& pos, MoveList* moveList);
