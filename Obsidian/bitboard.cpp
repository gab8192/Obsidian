#include <immintrin.h>
#include <iostream>

#include "bitboard.h"


/*
* The 2 squares diagonal to the pawn, which he can capture
*/
Bitboard pawn_attacks[COLOR_NB][SQUARE_NB];


// attacks

Bitboard RookMasks[SQUARE_NB];
Bitboard* RookAttacks[SQUARE_NB];

Bitboard BishopMasks[SQUARE_NB];
Bitboard* BishopAttacks[SQUARE_NB];

Bitboard king_attacks[SQUARE_NB];
Bitboard knight_attacks[SQUARE_NB];

Bitboard BishopTable[5248];
Bitboard RookTable[102400];


// other stuff

int SquareDistance[SQUARE_NB][SQUARE_NB];
int FileDistance[SQUARE_NB][SQUARE_NB];
int RankDistance[SQUARE_NB][SQUARE_NB];
Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
Bitboard LineBB[SQUARE_NB][SQUARE_NB];




// print bitboard
void print_bitboard(Bitboard bitboard)
{
    printf("\n");

    // loop over board ranks
    for (Rank y = RANK_8; y >= RANK_1; --y)
    {
        // print rank
        printf("  %d ", y+1);

        // loop over board files
        for (File x = FILE_A; x < FILE_NB; ++x)
        {
            // init board square
            Square square = make_square(x, y);

            // print bit indexed by board square
            printf(" %d", (bitboard & square) ? 1 : 0);
        }

        printf("\n");
    }

    // print files
    printf("\n     a b c d e f g h\n\n");

    // print bitboard as decimal
    printf("     bitboard: %llud\n", bitboard);

    std::cout << std::endl;
}

Bitboard mask_king_attacks(Square sqr) {
    Bitboard attacks = 0;
    File xLeft = (File) myMax(file_of(sqr) - 1, 0);
    File xRight = (File) myMin(file_of(sqr) + 1, 7);
    Rank yTop = (Rank) myMax(rank_of(sqr) - 1, 0);
    Rank yBottom = (Rank) myMin(rank_of(sqr) + 1, 7);

    for (Rank y = yTop; y <= yBottom; ++y) {
        for (File x = xLeft; x <= xRight; ++x) {
            Square dest = make_square(x, y);
            if (sqr != dest)
              attacks |= dest;
        }
    }
    return attacks;
}

struct KnightMove {
    int offX, offY;
};

const KnightMove KNIGHT_DIRS[] = {
    {2, 1}, {2, -1}, {-2, 1}, {-2, -1},
    {1, 2}, {1, -2}, {-1, 2}, {-1, -2} };

Bitboard mask_knight_attacks(Square sqr) {
    Bitboard attacks = 0;
    File x = file_of(sqr);
    Rank y = rank_of(sqr);
    for (int i = 0; i < 8; i++) {
        File destX = x + KNIGHT_DIRS[i].offX;
        Rank destY = y + KNIGHT_DIRS[i].offY;
        if (destX >= 0 && destX < 8 && destY >= 0 && destY < 8) {
            attacks |= make_square(destX, destY);
        }
    }
    return attacks;
}

/*
*/
Bitboard mask_pawn_attacks(Color pawnColor, Square sqr) {
    if (pawnColor == WHITE && rank_of(sqr) == RANK_8)
        return 0;
    if (pawnColor == BLACK && rank_of(sqr) == RANK_1)
        return 0;

    Bitboard attacks = 0;

    if (file_of(sqr) != FILE_A) { // then we can attack towards west
        attacks |= (pawnColor == WHITE ? (sqr + 7) : (sqr - 9));
    }
    if (file_of(sqr) != FILE_H) { // then we can attack towards east
        attacks |= (pawnColor == WHITE ? (sqr + 9) : (sqr - 7));
    }
    return attacks;
}

template<Color PawnColor>
Bitboard get_pawns_bb_attacks(Bitboard bb) {
  if constexpr (PawnColor == WHITE) {
    Bitboard east = (bb & ~FILE_HBB) << 9;
    Bitboard west = (bb & ~FILE_ABB) << 7;
    return east | west;
  }
  else {
    Bitboard east = (bb & ~FILE_HBB) >> 7;
    Bitboard west = (bb & ~FILE_ABB) >> 9;
    return east | west;
  }
}

template Bitboard get_pawns_bb_attacks<WHITE>(Bitboard bb);
template Bitboard get_pawns_bb_attacks<BLACK>(Bitboard bb);

int calcIncX(int direction) {
    switch (direction) {
    case EAST:
    case NORTH_EAST:
    case SOUTH_EAST: return 1;
    case WEST:
    case NORTH_WEST:
    case SOUTH_WEST: return -1;

    default: return 0;
    }
}

int calcIncY(int direction) {
    switch (direction) {
    case NORTH:
    case NORTH_EAST:
    case NORTH_WEST: return 1;
    case SOUTH:
    case SOUTH_EAST:
    case SOUTH_WEST: return -1;

    default: return 0;
    }
}

Bitboard sliding_attack(int dirs[], Square s1, Bitboard occupied)
{
    Bitboard attack = 0;

    for (int i = 0; i < 4; i++) {

        File destX = file_of(s1);
        Rank destY = rank_of(s1);

        int incX = calcIncX(dirs[i]);
        int incY = calcIncY(dirs[i]);

        while (true)
        {
            destX += incX;
            destY += incY;

            if (destX < 0 || destX > 7 || destY < 0 || destY > 7)
                break;

            attack |= make_square(destX, destY);

            if (occupied & make_square(destX, destY))
                break;
        }
    }

    return attack;
}


int RookDirs[] = { NORTH, EAST, SOUTH, WEST };
int BishopDirs[] = { NORTH_EAST, SOUTH_EAST, SOUTH_WEST, NORTH_WEST };

typedef uint32_t (AttackIndexFunc)(Square, Bitboard);

uint32_t bmi2_index_bishop(Square s, Bitboard occupied)
{
    return (uint32_t)_pext_u64(occupied, BishopMasks[s]);
}

uint32_t bmi2_index_rook(Square s, Bitboard occupied)
{
    return (uint32_t)_pext_u64(occupied, RookMasks[s]);
}

void init_bmi2(Bitboard table[], Bitboard* attacks[], Bitboard masks[],
    int deltas[], AttackIndexFunc index)
{
    Bitboard edges, b;

    for (Square s = SQ_A1; s < SQUARE_NB; ++s) {
        attacks[s] = table;

        // Board edges are not considered in the relevant occupancies
        edges = ((Rank1BB | Rank8BB) & ~rank_bb(s)) | ((FILE_ABB | FILE_HBB) & ~file_bb(s));

        masks[s] = sliding_attack(deltas, s, 0) & ~edges;

        // Use Carry-Rippler trick to enumerate all subsets of masks[s] and
        // fill the attacks table.
        b = 0;
        do {
            attacks[s][index(s, b)] = sliding_attack(deltas, s, b);
            b = (b - masks[s]) & masks[s];
            table++;
        } while (b);
    }
}

void bitboardsInit() {
    for (Square sqr = SQ_A1; sqr < SQUARE_NB; ++sqr) {
        king_attacks[sqr] = mask_king_attacks(sqr);

        knight_attacks[sqr] = mask_knight_attacks(sqr);

        pawn_attacks[WHITE][sqr] = mask_pawn_attacks(WHITE, sqr);
        pawn_attacks[BLACK][sqr] = mask_pawn_attacks(BLACK, sqr);

    }

    for (Square s1 = SQ_A1; s1 < SQUARE_NB; ++s1) {
        for (Square s2 = SQ_A1; s2 < SQUARE_NB; ++s2) {
            FileDistance[s1][s2] = abs(file_of(s1) - file_of(s2));
            RankDistance[s1][s2] = abs(rank_of(s1) - rank_of(s2));
            SquareDistance[s1][s2] = myMax(FileDistance[s1][s2], RankDistance[s1][s2]);
        }
    }


    // Init sliding attacks

    init_bmi2(RookTable, RookAttacks, RookMasks, RookDirs, bmi2_index_rook);
    init_bmi2(BishopTable, BishopAttacks, BishopMasks, BishopDirs, bmi2_index_bishop);



    memset(LineBB, 0, sizeof(LineBB));
    memset(BetweenBB, 0, sizeof(BetweenBB));

    for (Square s1 = SQ_A1; s1 < SQUARE_NB; ++s1) {
        for (Square s2 = SQ_A1; s2 < SQUARE_NB; ++s2) {
            if (get_bishop_attacks(s1) & s2) {
                BetweenBB[s1][s2] = get_bishop_attacks(s1, square_bb(s2)) & get_bishop_attacks(s2, square_bb(s1));

                LineBB[s1][s2] = (get_bishop_attacks(s1) & get_bishop_attacks(s2)) | s1 | s2;
            }
            else  if (get_rook_attacks(s1) & s2) {
                BetweenBB[s1][s2] = get_rook_attacks(s1, square_bb(s2)) & get_rook_attacks(s2, square_bb(s1));

                LineBB[s1][s2] = (get_rook_attacks(s1) & get_rook_attacks(s2)) | s1 | s2;
            }
            BetweenBB[s1][s2] |= s2;
        }
    }
}