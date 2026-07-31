#pragma once

#include "types.hpp"

namespace ah {

namespace Bitboards {

extern Bitboard SquareBB[SQUARE_NB];
extern Bitboard FileBB[FILE_NB];
extern Bitboard RankBB[RANK_NB];
extern Bitboard AdjacentFilesBB[FILE_NB];
extern Bitboard ForwardRanksBB[COLOR_NB][RANK_NB];
extern Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
extern Bitboard LineBB[SQUARE_NB][SQUARE_NB];
extern Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
extern Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];

void init();
Bitboard rook_attacks(Square s, Bitboard occupied);
Bitboard bishop_attacks(Square s, Bitboard occupied);

} // namespace Bitboards

inline Bitboard operator&(Bitboard b, Square s) { return b & Bitboards::SquareBB[s]; }
inline Bitboard operator|(Bitboard b, Square s) { return b | Bitboards::SquareBB[s]; }
inline Bitboard operator^(Bitboard b, Square s) { return b ^ Bitboards::SquareBB[s]; }
inline Bitboard& operator|=(Bitboard& b, Square s) { return b |= Bitboards::SquareBB[s]; }
inline Bitboard& operator^=(Bitboard& b, Square s) { return b ^= Bitboards::SquareBB[s]; }

Bitboard attacks_bb(PieceType pt, Square s, Bitboard occupied);
Bitboard pawn_attacks_bb(Color c, Bitboard b);

} // namespace ah
