#include "board/bitboard.hpp"

namespace ah {

namespace Bitboards {

Bitboard SquareBB[SQUARE_NB];
Bitboard FileBB[FILE_NB];
Bitboard RankBB[RANK_NB];
Bitboard AdjacentFilesBB[FILE_NB];
Bitboard ForwardRanksBB[COLOR_NB][RANK_NB];
Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
Bitboard LineBB[SQUARE_NB][SQUARE_NB];
Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];

static Bitboard safe_destination(Square s, int step) {
  Square to = Square(s + step);
  return is_ok(to) && std::abs(file_of(s) - file_of(to)) <= 2 ? square_bb(to) : 0;
}

static Bitboard sliding_attack(PieceType pt, Square sq, Bitboard occupied) {
  Bitboard attacks = 0;
  constexpr Direction RookDirections[4] = {NORTH, SOUTH, EAST, WEST};
  constexpr Direction BishopDirections[4] = {NORTH_EAST, NORTH_WEST, SOUTH_EAST, SOUTH_WEST};
  const Direction* dirs = (pt == ROOK) ? RookDirections : BishopDirections;
  for (int i = 0; i < 4; ++i) {
    Square s = sq;
    while (safe_destination(s, dirs[i])) {
      s = Square(s + dirs[i]);
      attacks |= s;
      if (occupied & s) break;
    }
  }
  return attacks;
}

void init() {
  for (Square s = SQ_A1; s < SQUARE_NB; ++s)
    SquareBB[s] = 1ULL << s;

  for (File f = FILE_A; f < FILE_NB; ++f)
    FileBB[f] = file_bb(f);
  for (Rank r = RANK_1; r < RANK_NB; ++r)
    RankBB[r] = rank_bb(r);

  for (File f = FILE_A; f < FILE_NB; ++f)
    AdjacentFilesBB[f] = (f > FILE_A ? FileBB[f - 1] : 0) | (f < FILE_H ? FileBB[f + 1] : 0);

  for (Rank r = RANK_1; r < RANK_NB; ++r) {
    Bitboard white = 0, black = 0;
    for (Rank rr = Rank(r + 1); rr < RANK_NB; ++rr) white |= RankBB[rr];
    for (Rank rr = RANK_1; rr < r; ++rr) black |= RankBB[rr];
    ForwardRanksBB[WHITE][r] = white;
    ForwardRanksBB[BLACK][r] = black;
  }

  for (Square s = SQ_A1; s < SQUARE_NB; ++s) {
    PawnAttacks[WHITE][s] = pawn_attacks_bb(WHITE, SquareBB[s]);
    PawnAttacks[BLACK][s] = pawn_attacks_bb(BLACK, SquareBB[s]);

    for (int step : {-17, -15, -10, -6, 6, 10, 15, 17})
      PseudoAttacks[KNIGHT][s] |= safe_destination(s, step);
    for (int step : {-9, -8, -7, -1, 1, 7, 8, 9})
      PseudoAttacks[KING][s] |= safe_destination(s, step);

    PseudoAttacks[BISHOP][s] = sliding_attack(BISHOP, s, 0);
    PseudoAttacks[ROOK][s] = sliding_attack(ROOK, s, 0);
    PseudoAttacks[QUEEN][s] = PseudoAttacks[BISHOP][s] | PseudoAttacks[ROOK][s];
  }

  for (Square s1 = SQ_A1; s1 < SQUARE_NB; ++s1) {
    for (PieceType pt : {BISHOP, ROOK}) {
      for (Square s2 = SQ_A1; s2 < SQUARE_NB; ++s2) {
        if (PseudoAttacks[pt][s1] & s2) {
          LineBB[s1][s2] = (sliding_attack(pt, s1, 0) & sliding_attack(pt, s2, 0)) | s1 | s2;
          BetweenBB[s1][s2] = sliding_attack(pt, s1, SquareBB[s2]) & sliding_attack(pt, s2, SquareBB[s1]);
        }
      }
    }
  }
}

} // namespace Bitboards

Bitboard pawn_attacks_bb(Color c, Bitboard b) {
  return c == WHITE
      ? ((b & ~file_bb(FILE_A)) << 7) | ((b & ~file_bb(FILE_H)) << 9)
      : ((b & ~file_bb(FILE_A)) >> 9) | ((b & ~file_bb(FILE_H)) >> 7);
}

Bitboard attacks_bb(PieceType pt, Square s, Bitboard occupied) {
  switch (pt) {
    case BISHOP: {
      Bitboard attacks = 0;
      for (Direction d : {NORTH_EAST, NORTH_WEST, SOUTH_EAST, SOUTH_WEST}) {
        Square sq = s;
        while (true) {
          Square to = Square(sq + d);
          if (!is_ok(to) || std::abs(file_of(sq) - file_of(to)) > 1) break;
          attacks |= to;
          if (occupied & to) break;
          sq = to;
        }
      }
      return attacks;
    }
    case ROOK: {
      Bitboard attacks = 0;
      for (Direction d : {NORTH, SOUTH, EAST, WEST}) {
        Square sq = s;
        while (true) {
          Square to = Square(sq + d);
          if (!is_ok(to) || (d == EAST || d == WEST) && std::abs(file_of(sq) - file_of(to)) != 1) break;
          if ((d == NORTH || d == SOUTH) && file_of(sq) != file_of(to)) break;
          attacks |= to;
          if (occupied & to) break;
          sq = to;
        }
      }
      return attacks;
    }
    case QUEEN:
      return attacks_bb(BISHOP, s, occupied) | attacks_bb(ROOK, s, occupied);
    default:
      return Bitboards::PseudoAttacks[pt][s];
  }
}

} // namespace ah
