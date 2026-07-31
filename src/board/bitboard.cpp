#include "board/bitboard.hpp"
#include <cmath>
#include <cstring>

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

// Sliding attack helpers via hyperbola quintessence
static Bitboard RookMasks[SQUARE_NB];
static Bitboard BishopMasks[SQUARE_NB];
static Bitboard RookAttacksMagics[SQUARE_NB][4096];
static Bitboard BishopAttacksMagics[SQUARE_NB][512];
static Bitboard RookMagics[SQUARE_NB];
static Bitboard BishopMagics[SQUARE_NB];
static int RookShifts[SQUARE_NB];
static int BishopShifts[SQUARE_NB];

static Bitboard safe_destination(Square s, int step) {
  Square to = Square(int(s) + step);
  return is_ok(to) && std::abs(file_of(s) - file_of(to)) <= 2 ? square_bb(to) : 0;
}

static Bitboard sliding_attack(PieceType pt, Square sq, Bitboard occupied) {
  Bitboard attacks = 0;
  const Direction RookDirections[4] = {NORTH, SOUTH, EAST, WEST};
  const Direction BishopDirections[4] = {NORTH_EAST, NORTH_WEST, SOUTH_EAST, SOUTH_WEST};
  const Direction* dirs = (pt == ROOK) ? RookDirections : BishopDirections;
  for (int i = 0; i < 4; ++i) {
    Square s = sq;
    while (safe_destination(s, dirs[i])) {
      s = Square(int(s) + int(dirs[i]));
      attacks |= s;
      if (occupied & s) break;
    }
  }
  return attacks;
}

static Bitboard index_to_occ(int index, int bits, Bitboard mask) {
  Bitboard occ = 0;
  for (int i = 0; i < bits; ++i) {
    Square sq = pop_lsb(mask);
    if (index & (1 << i)) occ |= square_bb(sq);
  }
  return occ;
}

static Bitboard find_magic(Square sq, int bits, PieceType pt) {
  Bitboard mask = (pt == ROOK) ? RookMasks[sq] : BishopMasks[sq];
  int n = 1 << bits;
  Bitboard occupancies[4096], attacks[4096], used[4096];
  for (int i = 0; i < n; ++i) {
    occupancies[i] = index_to_occ(i, bits, mask);
    attacks[i] = sliding_attack(pt, sq, occupancies[i]);
  }
  for (int k = 0; k < 100000000; ++k) {
    Bitboard magic = 0;
    // sparse random
    static uint64_t seed = 1070372;
    auto rand64 = [&]() {
      seed ^= seed >> 12; seed ^= seed << 25; seed ^= seed >> 27;
      return seed * 2685821657736338717ULL;
    };
    magic = rand64() & rand64() & rand64();
    if (popcount((mask * magic) >> 56) < 6) continue;
    std::memset(used, 0, sizeof(Bitboard) * n);
    bool fail = false;
    for (int i = 0; i < n; ++i) {
      int idx = int((occupancies[i] * magic) >> (64 - bits));
      if (used[idx] == 0) used[idx] = attacks[i];
      else if (used[idx] != attacks[i]) { fail = true; break; }
    }
    if (!fail) return magic;
  }
  return 0;
}

static void init_magics() {
  for (Square s = SQ_A1; s < SQUARE_NB; ++s) {
    Bitboard edges = ((rank_bb(RANK_1) | rank_bb(RANK_8)) & ~rank_bb(rank_of(s))) |
                     ((file_bb(FILE_A) | file_bb(FILE_H)) & ~file_bb(file_of(s)));
    RookMasks[s] = sliding_attack(ROOK, s, 0) & ~edges;
    BishopMasks[s] = sliding_attack(BISHOP, s, 0) & ~edges;
    RookShifts[s] = 64 - popcount(RookMasks[s]);
    BishopShifts[s] = 64 - popcount(BishopMasks[s]);
    RookMagics[s] = find_magic(s, popcount(RookMasks[s]), ROOK);
    BishopMagics[s] = find_magic(s, popcount(BishopMasks[s]), BISHOP);
    int nR = 1 << popcount(RookMasks[s]);
    int nB = 1 << popcount(BishopMasks[s]);
    for (int i = 0; i < nR; ++i) {
      Bitboard occ = index_to_occ(i, popcount(RookMasks[s]), RookMasks[s]);
      int idx = int((occ * RookMagics[s]) >> RookShifts[s]);
      RookAttacksMagics[s][idx] = sliding_attack(ROOK, s, occ);
    }
    for (int i = 0; i < nB; ++i) {
      Bitboard occ = index_to_occ(i, popcount(BishopMasks[s]), BishopMasks[s]);
      int idx = int((occ * BishopMagics[s]) >> BishopShifts[s]);
      BishopAttacksMagics[s][idx] = sliding_attack(BISHOP, s, occ);
    }
  }
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
    for (Rank rr = Rank(int(r) + 1); rr < RANK_NB; ++rr) white |= RankBB[rr];
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

  init_magics();
}

Bitboard rook_attacks(Square s, Bitboard occupied) {
  occupied &= RookMasks[s];
  occupied *= RookMagics[s];
  occupied >>= RookShifts[s];
  return RookAttacksMagics[s][occupied];
}

Bitboard bishop_attacks(Square s, Bitboard occupied) {
  occupied &= BishopMasks[s];
  occupied *= BishopMagics[s];
  occupied >>= BishopShifts[s];
  return BishopAttacksMagics[s][occupied];
}

} // namespace Bitboards

Bitboard pawn_attacks_bb(Color c, Bitboard b) {
  return c == WHITE
      ? ((b & ~file_bb(FILE_A)) << 7) | ((b & ~file_bb(FILE_H)) << 9)
      : ((b & ~file_bb(FILE_A)) >> 9) | ((b & ~file_bb(FILE_H)) >> 7);
}

Bitboard attacks_bb(PieceType pt, Square s, Bitboard occupied) {
  switch (pt) {
    case BISHOP: return Bitboards::bishop_attacks(s, occupied);
    case ROOK:   return Bitboards::rook_attacks(s, occupied);
    case QUEEN:  return Bitboards::bishop_attacks(s, occupied) | Bitboards::rook_attacks(s, occupied);
    default:     return Bitboards::PseudoAttacks[pt][s];
  }
}

} // namespace ah
