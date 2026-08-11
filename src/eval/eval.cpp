#include "eval/eval.hpp"
#include "nnue/nnue.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

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

Value classical_evaluate(const Position& pos) {
  int mg[COLOR_NB] = {}, eg[COLOR_NB] = {};

  for (Color c : {WHITE, BLACK}) {
    Bitboard b = pos.pieces(c);
    while (b) {
      Square s = pop_lsb(b);
      Piece pc = pos.piece_on(s);
      PieceType pt = type_of(pc);
      // PSTs are stored rank-8-first (visual); board squares are a1=0 — flip ranks.
      Square rel = Square(relative_square(c, s) ^ 56);
      mg[c] += PieceValueMg[pt] + MgPST[pt][rel];
      eg[c] += PieceValueEg[pt] + EgPST[pt][rel];
    }

    // Bishop pair
    if (popcount(pos.pieces(c, BISHOP)) >= 2) {
      mg[c] += 28;
      eg[c] += 43;
    }

    // Mobility (minors prefer squares not attacked by enemy pawns)
    Bitboard occ = pos.pieces();
    Bitboard safe = ~pos.pieces(c);
    Bitboard pawnSafe = safe & ~pawn_attacks_bb(~c, pos.pieces(~c, PAWN));
    Bitboard kn = pos.pieces(c, KNIGHT);
    while (kn) {
      int mob = popcount(Bitboards::PseudoAttacks[KNIGHT][pop_lsb(kn)] & pawnSafe);
      mg[c] += 5 * mob;
      eg[c] += 4 * mob;
    }
    Bitboard bi = pos.pieces(c, BISHOP);
    while (bi) {
      Square s = pop_lsb(bi);
      int mob = popcount(attacks_bb(BISHOP, s, occ) & pawnSafe);
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
    Bitboard enemyPawns = pos.pieces(~c, PAWN);
    while (pawns) {
      Square s = pop_lsb(pawns);
      File f = file_of(s);
      Rank r = relative_rank(c, s);

      Bitboard neighbors = Bitboards::AdjacentFilesBB[f] & ourPawns;
      if (!neighbors) {
        mg[c] -= isolated_penalty_mg;
        eg[c] -= isolated_penalty_eg;
      }

      // Supported / phalanx
      Bitboard support = Bitboards::PawnAttacks[~c][s] & ourPawns;
      Bitboard phalanx = neighbors & rank_bb(rank_of(s));
      if (support | phalanx) {
        mg[c] += 4 + int(r);
        eg[c] += 3 + int(r) / 2;
      }

      // Backward pawn: no neighbor able to protect advance, enemy controls stop square
      Square stop = s + pawn_push(c);
      if (is_ok(stop) && !(neighbors & Bitboards::ForwardRanksBB[~c][rank_of(s)]) &&
          (Bitboards::PawnAttacks[c][stop] & enemyPawns) && !(ourPawns & square_bb(stop))) {
        mg[c] -= 6;
        eg[c] -= 10;
      }

      Bitboard forward = Bitboards::ForwardRanksBB[c][rank_of(s)];
      Bitboard passed_mask = forward & (file_bb(f) | Bitboards::AdjacentFilesBB[f]);
      if (!(enemyPawns & passed_mask)) {
        int bonus_mg = passed_bonus_mg[r];
        int bonus_eg = passed_bonus_eg[r];
        if (support) {
          bonus_mg += 8 + 4 * int(r);
          bonus_eg += 12 + 8 * int(r);
        }
        // King proximity to passer (endgame)
        Square ksq = pos.king_square(c);
        Square eksq = pos.king_square(~c);
        int ourDist = std::max(std::abs(file_of(ksq) - file_of(s)), std::abs(rank_of(ksq) - rank_of(s)));
        int theirDist = std::max(std::abs(file_of(eksq) - file_of(s)), std::abs(rank_of(eksq) - rank_of(s)));
        bonus_eg += (theirDist - ourDist) * (2 + int(r));
        // Free path to promotion
        Bitboard path = forward & file_bb(f);
        if (!(pos.pieces() & path)) {
          bonus_mg += 5 * int(r);
          bonus_eg += 12 * int(r);
        }
        // Rook behind passer
        if (pos.pieces(c, ROOK) & file_bb(f) & Bitboards::ForwardRanksBB[~c][rank_of(s)]) {
          bonus_mg += 10;
          bonus_eg += 25;
        }
        mg[c] += bonus_mg;
        eg[c] += bonus_eg;
      }
    }
    for (File f = FILE_A; f <= FILE_H; ++f) {
      int cnt = popcount(ourPawns & file_bb(f));
      if (cnt > 1) {
        mg[c] -= (cnt - 1) * doubled_penalty_mg;
        eg[c] -= (cnt - 1) * doubled_penalty_eg;
      }
    }

    // Knight / bishop outposts on protected central squares
    Bitboard outpostMask = (c == WHITE)
        ? (rank_bb(RANK_4) | rank_bb(RANK_5) | rank_bb(RANK_6))
        : (rank_bb(RANK_5) | rank_bb(RANK_4) | rank_bb(RANK_3));
    outpostMask &= (file_bb(FILE_C) | file_bb(FILE_D) | file_bb(FILE_E) | file_bb(FILE_F));
    Bitboard knOut = pos.pieces(c, KNIGHT) & outpostMask;
    while (knOut) {
      Square s = pop_lsb(knOut);
      if (Bitboards::PawnAttacks[~c][s] & ourPawns) {
        // Not attackable by enemy pawn
        Bitboard ahead = Bitboards::ForwardRanksBB[c][rank_of(s)] & Bitboards::AdjacentFilesBB[file_of(s)];
        if (!(enemyPawns & ahead)) {
          mg[c] += 28;
          eg[c] += 18;
        }
      }
    }
    Bitboard biOut = pos.pieces(c, BISHOP) & outpostMask;
    while (biOut) {
      Square s = pop_lsb(biOut);
      if (Bitboards::PawnAttacks[~c][s] & ourPawns) {
        Bitboard ahead = Bitboards::ForwardRanksBB[c][rank_of(s)] & Bitboards::AdjacentFilesBB[file_of(s)];
        if (!(enemyPawns & ahead)) {
          mg[c] += 18;
          eg[c] += 10;
        }
      }
    }

    // Space: friendly pawns controlling advanced central squares
    Bitboard spaceArea = (c == WHITE)
        ? (rank_bb(RANK_2) | rank_bb(RANK_3) | rank_bb(RANK_4))
        : (rank_bb(RANK_7) | rank_bb(RANK_6) | rank_bb(RANK_5));
    spaceArea &= (file_bb(FILE_C) | file_bb(FILE_D) | file_bb(FILE_E) | file_bb(FILE_F));
    mg[c] += 2 * popcount(ourPawns & spaceArea);

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
    // Shelter: nearest friendly pawn distance on king file ±1 (far pawns count less)
    int shelterScore = 0;
    for (int ff = std::max(0, int(kf) - 1); ff <= std::min(7, int(kf) + 1); ++ff) {
      Bitboard filePawns = pos.pieces(c, PAWN) & file_bb(File(ff)) &
                           Bitboards::ForwardRanksBB[c][rank_of(ksq)];
      if (!filePawns) {
        shelterScore -= 14;
        continue;
      }
      int bestDist = 8;
      Bitboard fp = filePawns;
      while (fp) {
        Square s = pop_lsb(fp);
        bestDist = std::min(bestDist, std::abs(int(rank_of(s)) - int(rank_of(ksq))));
      }
      shelterScore += std::max(0, 18 - 5 * bestDist);
    }
    mg[c] += shelterScore;
    if ((kf <= FILE_C || kf >= FILE_G) && shelterScore < 0 && relative_rank(c, ksq) == RANK_1)
      mg[c] -= 20;

    // Pawn storm: closer enemy-oriented pawns are more dangerous
    Square eks = pos.king_square(~c);
    File ekf = file_of(eks);
    Bitboard stormFiles = file_bb(ekf) | Bitboards::AdjacentFilesBB[ekf];
    Bitboard stormers = ourPawns & stormFiles & Bitboards::ForwardRanksBB[~c][rank_of(eks)];
    while (stormers) {
      Square s = pop_lsb(stormers);
      int dist = std::abs(int(rank_of(s)) - int(rank_of(eks)));
      mg[c] += std::max(0, 22 - 5 * dist);
    }

    // King safety: weighted attackers on king ring
    Bitboard zone = Bitboards::PseudoAttacks[KING][ksq] | square_bb(ksq);
    int attackUnits = 0;
    int attackerCount = 0;
    Bitboard knA = pos.pieces(~c, KNIGHT);
    while (knA) {
      if (Bitboards::PseudoAttacks[KNIGHT][pop_lsb(knA)] & zone) { attackUnits += 2; ++attackerCount; }
    }
    Bitboard biA = pos.pieces(~c, BISHOP);
    while (biA) {
      Square s = pop_lsb(biA);
      if (attacks_bb(BISHOP, s, occ) & zone) { attackUnits += 2; ++attackerCount; }
    }
    Bitboard roA = pos.pieces(~c, ROOK);
    while (roA) {
      Square s = pop_lsb(roA);
      if (attacks_bb(ROOK, s, occ) & zone) { attackUnits += 3; ++attackerCount; }
    }
    Bitboard quA = pos.pieces(~c, QUEEN);
    while (quA) {
      Square s = pop_lsb(quA);
      if (attacks_bb(QUEEN, s, occ) & zone) { attackUnits += 5; ++attackerCount; }
    }
    if (pawn_attacks_bb(~c, pos.pieces(~c, PAWN)) & zone) { attackUnits += 1; ++attackerCount; }
    // Open files near king amplify danger
    if (!(pos.pieces(PAWN) & file_bb(kf))) attackUnits += 2;
    if (attackerCount >= 2)
      mg[c] -= attackUnits * attackUnits / 3 + 3 * attackerCount;

    // Threats: best winning opponent capture per our piece (mutually exclusive victims)
    Bitboard ours = pos.pieces(c, PAWN) | pos.pieces(c, KNIGHT) | pos.pieces(c, BISHOP)
                  | pos.pieces(c, ROOK) | pos.pieces(c, QUEEN);
    int bestThreat[SQUARE_NB] = {};
    Bitboard opp = pos.pieces(~c) & ~pos.pieces(~c, KING);
    while (opp) {
      Square from = pop_lsb(opp);
      PieceType apt = type_of(pos.piece_on(from));
      Bitboard targets;
      if (apt == PAWN) targets = Bitboards::PawnAttacks[~c][from] & ours;
      else if (apt == KNIGHT) targets = Bitboards::PseudoAttacks[KNIGHT][from] & ours;
      else if (apt == BISHOP) targets = attacks_bb(BISHOP, from, occ) & ours;
      else if (apt == ROOK) targets = attacks_bb(ROOK, from, occ) & ours;
      else if (apt == QUEEN) targets = attacks_bb(QUEEN, from, occ) & ours;
      else continue;
      while (targets) {
        Square to = pop_lsb(targets);
        PieceType vpt = type_of(pos.piece_on(to));
        if (PieceValueMg[vpt] < PieceValueMg[apt]) continue;
        Move threat(from, to);
        if (pos.see_ge(threat, 0)) {
          int pen = (PieceValueMg[vpt] - PieceValueMg[apt] / 2) / 2;
          pen = std::clamp(pen, 20, 180);
          bestThreat[to] = std::max(bestThreat[to], pen);
        }
      }
    }
    Bitboard threatened = ours;
    while (threatened) {
      Square to = pop_lsb(threatened);
      if (bestThreat[to]) {
        mg[c] -= bestThreat[to];
        eg[c] -= bestThreat[to] * 2 / 3;
      }
    }

    // Endgame mop-up: only the side that's ahead benefits from driving kings together
    // (identical penalties on both colors previously cancelled to zero).
    Square eksq = pos.king_square(~c);
    int dist = std::abs(file_of(ksq) - file_of(eksq)) + std::abs(rank_of(ksq) - rank_of(eksq));
    eg[c] -= dist; // small base activity; advantage term applied after blend
  }

  // Encourage castled king positions already via PST; discourage early king walks
  for (Color c : {WHITE, BLACK}) {
    Square ksq = pos.king_square(c);
    if (relative_rank(c, ksq) >= RANK_3 && pos.non_pawn_material() > 2000)
      mg[c] -= 40 * (relative_rank(c, ksq) - RANK_2);
  }

  // phase: 0 = middlegame-ish material present, 24 = bare kings
  int phase = phase_weight(pos);
  int egw = phase;
  int mgw = 24 - phase;
  int score = ((mg[WHITE] - mg[BLACK]) * mgw + (eg[WHITE] - eg[BLACK]) * egw) / 24;

  // Advantage-dependent mop-up: when clearly ahead in the endgame, chase the enemy king
  // and push passed pawns / restrict the defending king to the rim.
  if (egw >= 12) {
    Square wk = pos.king_square(WHITE), bk = pos.king_square(BLACK);
    int kdist = std::abs(file_of(wk) - file_of(bk)) + std::abs(rank_of(wk) - rank_of(bk));
    auto rim = [](Square s) {
      int f = std::min(int(file_of(s)), 7 - int(file_of(s)));
      int r = std::min(int(rank_of(s)), 7 - int(rank_of(s)));
      return f + r;
    };
    if (score > 120) {
      score += (14 - kdist) * (egw / 6);
      score += (7 - rim(bk)) * (egw / 10); // milder than v30 (which used /8 and hurt)
      // Encourage advancing our furthest passer when winning.
      Bitboard wp = pos.pieces(WHITE, PAWN);
      while (wp) {
        Square s = pop_lsb(wp);
        int rr = int(rank_of(s));
        if (rr >= RANK_5) score += (rr - RANK_4) * (egw / 8);
      }
    } else if (score < -120) {
      score -= (14 - kdist) * (egw / 6);
      score -= (7 - rim(wk)) * (egw / 10);
      Bitboard bp = pos.pieces(BLACK, PAWN);
      while (bp) {
        Square s = pop_lsb(bp);
        int rr = 7 - int(rank_of(s));
        if (rr >= RANK_5) score -= (rr - RANK_4) * (egw / 8);
      }
    }
  }

  // Opposite-colored bishops: more drawish in endgames
  if (popcount(pos.pieces(BISHOP)) == 2 &&
      popcount(pos.pieces(WHITE, BISHOP)) == 1 &&
      popcount(pos.pieces(BLACK, BISHOP)) == 1) {
    Bitboard wb = pos.pieces(WHITE, BISHOP);
    Bitboard bb = pos.pieces(BLACK, BISHOP);
    Square ws = lsb(wb), bs = lsb(bb);
    if (((int(file_of(ws)) + int(rank_of(ws))) & 1) != ((int(file_of(bs)) + int(rank_of(bs))) & 1)) {
      if (pos.non_pawn_material() <= 2 * 365)
        score = score * 2 / 3;
    }
  }

  // Tempo: side-to-move advantage (score is White-relative until return)
  {
    int tempo = (40 * mgw) / 24;
    score += (pos.side_to_move() == WHITE ? tempo : -tempo);
  }

  // Exact insufficient-material draws / near-draws
  const int wp = popcount(pos.pieces(WHITE, PAWN));
  const int bp = popcount(pos.pieces(BLACK, PAWN));
  const int wn = popcount(pos.pieces(WHITE, KNIGHT));
  const int bn = popcount(pos.pieces(BLACK, KNIGHT));
  const int wb = popcount(pos.pieces(WHITE, BISHOP));
  const int bb = popcount(pos.pieces(BLACK, BISHOP));
  const int wr = popcount(pos.pieces(WHITE, ROOK));
  const int br = popcount(pos.pieces(BLACK, ROOK));
  const int wq = popcount(pos.pieces(WHITE, QUEEN));
  const int bq = popcount(pos.pieces(BLACK, QUEEN));
  if (!wp && !bp && !wr && !br && !wq && !bq) {
    // K vs K, KB/KN vs K, KNN vs K
    int minorsW = wn + wb, minorsB = bn + bb;
    if (minorsW + minorsB <= 1) score = 0;
    else if (minorsW == 2 && !minorsB && wb == 0 && wn == 2) score = 0;
    else if (minorsB == 2 && !minorsW && bb == 0 && bn == 2) score = 0;
    else if (minorsW <= 1 && minorsB <= 1) score = score / 8;
  }

  // Note: do not scale by rule50 here — TT key ignores rule50.

  return Value(pos.side_to_move() == WHITE ? score : -score);
}

Value evaluate(const Position& pos) {
  return evaluate(pos, nullptr);
}

Value evaluate(const Position& pos, const NnueAccumulator* acc) {
  // Classical is the strength default. NNUE only when ADITYA_USE_NNUE=1 and loaded.
  static int use_nnue = -1;
  static int blend = -1; // percent classical, default 70
  if (use_nnue < 0) {
    const char* e = std::getenv("ADITYA_USE_NNUE");
    use_nnue = (e && e[0] == '1') ? 1 : 0;
    const char* b = std::getenv("ADITYA_NNUE_BLEND");
    blend = b ? std::clamp(std::atoi(b), 0, 100) : 70;
  }
  if (use_nnue && nnue_ready() && blend < 100) {
    // Skip net when blend=100 (pure classical) so NPS is not destroyed.
    Value net = (acc && acc->computed) ? nnue().evaluate(pos, *acc) : nnue().evaluate(pos);
    if (net != VALUE_NONE) {
      if (blend <= 0) return net;
      Value classical = classical_evaluate(pos);
      return Value((int(classical) * blend + int(net) * (100 - blend)) / 100);
    }
  }
  return classical_evaluate(pos);
}

} // namespace ah
