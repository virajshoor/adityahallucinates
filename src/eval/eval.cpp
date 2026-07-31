#include "eval/eval.hpp"
#include <algorithm>
#include <cmath>

namespace ah {

namespace {

constexpr int PawnValueMg = 82, PawnValueEg = 94;
constexpr int KnightValueMg = 337, KnightValueEg = 281;
constexpr int BishopValueMg = 365, BishopValueEg = 297;
constexpr int RookValueMg = 477, RookValueEg = 512;
constexpr int QueenValueMg = 1025, QueenValueEg = 936;

constexpr int PieceValueMg[PIECE_TYPE_NB] = {0, PawnValueMg, KnightValueMg, BishopValueMg, RookValueMg, QueenValueMg, 0};
constexpr int PieceValueEg[PIECE_TYPE_NB] = {0, PawnValueEg, KnightValueEg, BishopValueEg, RookValueEg, QueenValueEg, 0};

// PeSTO-inspired middlegame / endgame PSTs (mirrored for black via relative_square)
constexpr int PawnMg[64] = {
   0,  0,  0,  0,  0,  0,  0,  0,
  98,134, 61, 95, 68,126, 34,-11,
  -6,  7, 26, 31, 65, 56, 25,-20,
 -14, 13,  6, 21, 23, 12, 17,-23,
 -27, -2, -5, 12, 17,  6, 10,-25,
 -26, -4, -4,-10,  3,  3, 33,-12,
 -35, -1,-20,-23,-15, 24, 38,-22,
   0,  0,  0,  0,  0,  0,  0,  0
};
constexpr int PawnEg[64] = {
   0,  0,  0,  0,  0,  0,  0,  0,
 178,173,158,134,147,132,165,187,
  94,100, 85, 67, 56, 53, 82, 84,
  32, 24, 13,  5, -2,  4, 17, 17,
  13,  9, -3, -7, -7, -8,  3, -1,
   4,  7, -6,  1,  0, -5, -1, -8,
  13,  8,  8, 10, 13,  0,  2, -7,
   0,  0,  0,  0,  0,  0,  0,  0
};
constexpr int KnightMg[64] = {
 -167,-89,-34,-49, 61,-97,-15,-107,
  -73,-41, 72, 36, 23, 62,  7, -17,
  -47, 60, 37, 65, 84,129, 73,  44,
   -9, 17, 19, 53, 37, 69, 18,  22,
  -13,  4, 16, 13, 28, 19, 21,  -8,
  -23,-17, 12, 10, 19, 17, 25, -16,
  -29,-53,-12, -3, -1, 18,-14, -19,
 -105,-21,-58,-33,-17,-28,-19, -23
};
constexpr int KnightEg[64] = {
  -58,-38,-13,-28,-31,-27,-63,-99,
  -25, -8,-25, -2, -9,-25,-24,-52,
  -24,-20, 10,  9, -1, -9,-19,-41,
  -17,  3, 22, 22, 22, 11,  8,-18,
  -18, -6, 16, 25, 16, 17,  4,-18,
  -23, -3, -1, 15, 10, -3,-20,-22,
  -42,-20,-10, -5, -2,-20,-23,-44,
  -29,-51,-23,-15,-22,-18,-50,-64
};
constexpr int BishopMg[64] = {
  -29,  4,-82,-37,-25,-42,  7, -8,
  -26, 16,-18,-13, 30, 59, 18,-47,
  -16, 37, 43, 40, 35, 50, 37, -2,
   -4,  5, 19, 50, 37, 37,  7, -2,
   -6, 13, 13, 26, 34, 12, 10,  4,
    0, 15, 15, 15, 14, 27, 18, 10,
    4, 15, 16,  0,  7, 21, 33,  1,
  -33, -3,-14,-21,-13,-12,-39,-21
};
constexpr int BishopEg[64] = {
  -14,-21,-11, -8, -7, -9,-17,-24,
   -8, -4,  7,-12, -3,-13, -4,-14,
    2, -8,  0, -1, -2,  6,  0,  4,
   -3,  9, 12,  9, 14, 10,  3,  2,
   -6,  3, 13, 19,  7, 10, -3, -9,
  -12, -3,  8, 10, 13,  3,-7,-15,
  -14,-18, -7, -1,  4, -9,-15,-27,
  -23, -9,-23, -5, -9,-16, -5,-17
};
constexpr int RookMg[64] = {
   32, 42, 32, 51, 63,  9, 31, 43,
   27, 32, 58, 62, 80, 67, 26, 44,
   -5, 19, 26, 36, 17, 45, 61, 16,
  -24,-11,  7, 26, 24, 35, -8,-20,
  -36,-26,-12, -1,  9, -7,  6,-23,
  -45,-25,-16,-17,  3,  0, -5,-33,
  -44,-16,-20, -9, -1, 11, -6,-71,
  -19,-13,  1, 17, 16,  7,-37,-26
};
constexpr int RookEg[64] = {
   13, 10, 18, 15, 12, 12,  8,  5,
   11, 13, 13, 11, -1,  3,  8,  3,
    7,  7,  7,  5,  4, -3, -5, -3,
    4,  3, 13,  1,  2,  1, -1,  2,
    3,  5,  8,  4, -5, -6, -8,-11,
   -4,  0, -5, -1, -7,-12, -8,-16,
   -6, -6,  0,  2, -9, -9,-11, -3,
   -9,  2,  3, -1, -5,-13,  4,-20
};
constexpr int QueenMg[64] = {
  -28,  0, 29, 12, 59, 44, 43, 45,
  -24,-39,-5,  1,-16, 57, 28, 54,
  -13,-17,  7,  8, 29, 56, 47, 57,
  -27,-27,-16,-16, -1, 17, -2,  1,
   -9,-26, -9,-10, -2, -4,  3, -3,
  -14,  2,-11, -2, -5,  2, 14,  5,
  -35, -8, 11,  2,  8, 15, -3,  1,
   -1,-18, -9, 10,-15,-25,-31,-50
};
constexpr int QueenEg[64] = {
   -9, 22, 22, 27, 27, 19, 10, 20,
  -17, 20, 32, 41, 58, 25, 30,  0,
  -20,  6,  9, 49, 47, 35, 19,  9,
    3, 22, 24, 45, 57, 40, 57, 36,
  -18, 28, 19, 47, 31, 34, 39, 23,
  -16,-27, 15,  6,  9, 17, 10,  5,
  -22,-23,-30,-16,-16,-23,-36,-32,
  -33,-28,-22,-43, -5,-32,-20,-41
};
constexpr int KingMg[64] = {
  -65, 23, 16,-15,-56,-34,  2, 13,
   29, -1,-20, -7, -8, -4,-38,-29,
   -9, 24,  2,-16,-20,  6, 22,-22,
  -17,-20,-12,-27,-30,-25,-14,-36,
  -49, -1,-27,-39,-46,-44,-33,-51,
  -14,-14,-22,-46,-44,-30,-15,-27,
    1,  7, -8,-64,-43,-16,  9,  8,
  -15, 36, 12,-54,  8,-28, 24, 14
};
constexpr int KingEg[64] = {
  -74,-35,-18,-18,-11, 15,  4,-17,
  -12, 17, 14, 17, 17, 38, 23, 11,
   10, 17, 23, 15, 20, 45, 44, 13,
   -8, 22, 24, 27, 26, 33, 26,  3,
  -18, -4, 21, 24, 27, 23,  9,-11,
  -19, -3, 11, 21, 23, 16,  7, -9,
  -27,-11,  4, 13, 14,  4, -5,-17,
  -53,-34,-21,-11,-28,-14,-24,-43
};

const int* MgPST[PIECE_TYPE_NB] = {nullptr, PawnMg, KnightMg, BishopMg, RookMg, QueenMg, KingMg};
const int* EgPST[PIECE_TYPE_NB] = {nullptr, PawnEg, KnightEg, BishopEg, RookEg, QueenEg, KingEg};

int phase_weight(const Position& pos) {
  int phase = 24;
  phase -= popcount(pos.pieces(KNIGHT));
  phase -= popcount(pos.pieces(BISHOP));
  phase -= 2 * popcount(pos.pieces(ROOK));
  phase -= 4 * popcount(pos.pieces(QUEEN));
  return std::clamp(phase, 0, 24);
}

int isolated_penalty_mg = 5, isolated_penalty_eg = 15;
int doubled_penalty_mg = 11, doubled_penalty_eg = 56;
int passed_bonus_mg[8] = {0, 5, 12, 24, 40, 70, 110, 0};
int passed_bonus_eg[8] = {0, 15, 30, 55, 90, 150, 260, 0};

} // namespace

Value evaluate(const Position& pos) {
  int mg[COLOR_NB] = {}, eg[COLOR_NB] = {};

  for (Color c : {WHITE, BLACK}) {
    Bitboard b = pos.pieces(c);
    while (b) {
      Square s = pop_lsb(b);
      Piece pc = pos.piece_on(s);
      PieceType pt = type_of(pc);
      Square rel = relative_square(c, s);
      mg[c] += PieceValueMg[pt] + MgPST[pt][rel];
      eg[c] += PieceValueEg[pt] + EgPST[pt][rel];
    }

    // Bishop pair
    if (popcount(pos.pieces(c, BISHOP)) >= 2) {
      mg[c] += 28;
      eg[c] += 43;
    }

    // Mobility
    Bitboard occ = pos.pieces();
    Bitboard safe = ~pos.pieces(c);
    Bitboard kn = pos.pieces(c, KNIGHT);
    while (kn) {
      int mob = popcount(Bitboards::PseudoAttacks[KNIGHT][pop_lsb(kn)] & safe);
      mg[c] += 4 * mob;
      eg[c] += 4 * mob;
    }
    Bitboard bi = pos.pieces(c, BISHOP);
    while (bi) {
      Square s = pop_lsb(bi);
      int mob = popcount(attacks_bb(BISHOP, s, occ) & safe);
      mg[c] += 5 * mob;
      eg[c] += 5 * mob;
    }
    Bitboard ro = pos.pieces(c, ROOK);
    while (ro) {
      Square s = pop_lsb(ro);
      int mob = popcount(attacks_bb(ROOK, s, occ) & safe);
      mg[c] += 3 * mob;
      eg[c] += 5 * mob;
    }
    Bitboard qu = pos.pieces(c, QUEEN);
    while (qu) {
      Square s = pop_lsb(qu);
      int mob = popcount(attacks_bb(QUEEN, s, occ) & safe);
      mg[c] += 1 * mob;
      eg[c] += 3 * mob;
    }

    // Pawn structure
    Bitboard pawns = pos.pieces(c, PAWN);
    Bitboard ourPawns = pawns;
    while (pawns) {
      Square s = pop_lsb(pawns);
      File f = file_of(s);
      Rank r = relative_rank(c, s);

      Bitboard neighbors = Bitboards::AdjacentFilesBB[f] & ourPawns;
      if (!neighbors) {
        mg[c] -= isolated_penalty_mg;
        eg[c] -= isolated_penalty_eg;
      }
      if (more_than_one(ourPawns & file_bb(f))) {
        // count once per file roughly
      }

      Bitboard forward = Bitboards::ForwardRanksBB[c][rank_of(s)];
      Bitboard passed_mask = forward & (file_bb(f) | Bitboards::AdjacentFilesBB[f]);
      if (!(pos.pieces(~c, PAWN) & passed_mask)) {
        mg[c] += passed_bonus_mg[r];
        eg[c] += passed_bonus_eg[r];
      }
    }
    for (File f = FILE_A; f <= FILE_H; ++f) {
      int cnt = popcount(ourPawns & file_bb(f));
      if (cnt > 1) {
        mg[c] -= (cnt - 1) * doubled_penalty_mg;
        eg[c] -= (cnt - 1) * doubled_penalty_eg;
      }
    }

    // Rook on open/semi-open file
    Bitboard rooks = pos.pieces(c, ROOK);
    while (rooks) {
      Square s = pop_lsb(rooks);
      File f = file_of(s);
      if (!(pos.pieces(PAWN) & file_bb(f))) {
        mg[c] += 25; eg[c] += 15;
      } else if (!(pos.pieces(c, PAWN) & file_bb(f))) {
        mg[c] += 12; eg[c] += 6;
      }
      // Rook on 7th
      if (relative_rank(c, s) == RANK_7) {
        mg[c] += 20; eg[c] += 30;
      }
    }

    // Castling rights / king shelter
    Square ksq = pos.king_square(c);
    if (pos.can_castle(c == WHITE ? WHITE_OO : BLACK_OO)) mg[c] += 20;
    if (pos.can_castle(c == WHITE ? WHITE_OOO : BLACK_OOO)) mg[c] += 10;

    File kf = file_of(ksq);
    Bitboard shelterMask = file_bb(kf) | Bitboards::AdjacentFilesBB[kf];
    Bitboard shelterPawns = pos.pieces(c, PAWN) &
        Bitboards::ForwardRanksBB[c][rank_of(ksq)] & shelterMask;
    int shelter = popcount(shelterPawns);
    mg[c] += 8 * std::min(3, shelter);
    if ((kf <= FILE_C || kf >= FILE_G) && shelter == 0 && relative_rank(c, ksq) == RANK_1)
      mg[c] -= 20;

    // King safety: weighted attackers
    Bitboard zone = Bitboards::PseudoAttacks[KING][ksq] | square_bb(ksq);
    int attackUnits = 0;
    int attackerCount = 0;
    auto add_attacks = [&](PieceType pt, int weight) {
      Bitboard bb = pos.pieces(~c, pt);
      while (bb) {
        Square s = pop_lsb(bb);
        Bitboard atk = (pt == PAWN) ? Bitboards::PawnAttacks[~c][s]
                     : (pt == KNIGHT || pt == KING) ? Bitboards::PseudoAttacks[pt][s]
                     : attacks_bb(pt, s, pos.pieces());
        if (atk & zone) {
          attackUnits += weight;
          ++attackerCount;
        }
      }
    };
    add_attacks(PAWN, 1);
    add_attacks(KNIGHT, 2);
    add_attacks(BISHOP, 2);
    add_attacks(ROOK, 3);
    add_attacks(QUEEN, 5);
    if (attackerCount >= 2)
      mg[c] -= attackUnits * attackUnits;

    // Hanging pieces (undefended and attacked) — sample non-pawns only, cheap check
    Bitboard ours = pos.pieces(c, KNIGHT) | pos.pieces(c, BISHOP) | pos.pieces(c, ROOK) | pos.pieces(c, QUEEN);
    Bitboard attacked = 0;
    // rough: enemy pawn attacks + knight attacks as proxy for speed
    attacked |= pawn_attacks_bb(~c, pos.pieces(~c, PAWN));
    Bitboard ek = pos.pieces(~c, KNIGHT);
    while (ek) attacked |= Bitboards::PseudoAttacks[KNIGHT][pop_lsb(ek)];
    Bitboard hang = ours & attacked;
    while (hang) {
      Square s = pop_lsb(hang);
      if (!(pos.attackers_to(s, c))) {
        int pen = PieceValueMg[type_of(pos.piece_on(s))] / 5;
        mg[c] -= pen;
        eg[c] -= pen / 2;
      }
    }

    // Endgame king activity toward center already in PST; nudge toward enemy king
    Square eksq = pos.king_square(~c);
    int dist = std::abs(file_of(ksq) - file_of(eksq)) + std::abs(rank_of(ksq) - rank_of(eksq));
    eg[c] -= 4 * dist;
  }

  // phase: 0 = middlegame-ish material present, 24 = bare kings
  int phase = phase_weight(pos);
  int egw = phase;
  int mgw = 24 - phase;
  int score = ((mg[WHITE] - mg[BLACK]) * mgw + (eg[WHITE] - eg[BLACK]) * egw) / 24;

  // Tempo + contempt to prefer decisive play over threefolds
  score += 35;

  // Encourage castled king positions already via PST; discourage early king walks
  for (Color c : {WHITE, BLACK}) {
    Square ksq = pos.king_square(c);
    if (relative_rank(c, ksq) >= RANK_3 && pos.non_pawn_material() > 2000)
      mg[c] -= 40 * (relative_rank(c, ksq) - RANK_2);
  }

  return Value(pos.side_to_move() == WHITE ? score : -score);
}

} // namespace ah
