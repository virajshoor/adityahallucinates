#include "search/book.hpp"
#include "movegen/movegen.hpp"
#include <random>
#include <string>
#include <vector>

namespace ah {

namespace {

struct BookEntry {
  const char* fen;
  const char* uci;
  int weight;
};

// Weighted mainline book (piece placement + side to move)
const BookEntry kBook[] = {
  // Start — prefer classical central opens
  {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w", "e2e4", 55},
  {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w", "d2d4", 40},
  {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w", "g1f3", 8},
  {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w", "c2c4", 5},

  // After 1.e4 — prefer e5/Caro; French Winawer collapsed Elo3000 (v46 g2)
  {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b", "e7e5", 62},
  {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b", "c7c6", 25},
  {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b", "c7c5", 10},
  {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b", "e7e6", 5},

  // After 1.d4 — prefer solid classical replies (avoid soft sidelines / French-by-transposition)
  {"rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b", "d7d5", 52},
  {"rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b", "g8f6", 48},
  // Intentionally omit ...e6 here — 1.d4 e6 2.e4 drifts into weak French lines for us

  // QGD / Slav structures: develop before ...h6
  {"rnbqkb1r/ppp1pppp/5n2/3p4/2PP4/2N5/PP2PPPP/R1BQKBNR b", "e7e6", 50},
  {"rnbqkb1r/ppp1pppp/5n2/3p4/2PP4/2N5/PP2PPPP/R1BQKBNR b", "c7c6", 40},
  {"rnbqkbnr/ppp2ppp/4p3/3p4/2PP4/2N5/PP2PPPP/R1BQKBNR b", "g8f6", 70},
  {"rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/2N5/PP2PPPP/R1BQKBNR w", "c1g5", 40},
  {"rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/2N5/PP2PPPP/R1BQKBNR w", "g1f3", 45},
  // Critical: after 1.d4 d5 2.Nf3 Nf6 3.c4 e6 4.Nc3 (or move-order twin) — NOT ...h6
  {"rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/2N2N2/PP2PPPP/R1BQKB1R b", "f8e7", 45},
  {"rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/2N2N2/PP2PPPP/R1BQKB1R b", "f8b4", 25},
  {"rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/2N2N2/PP2PPPP/R1BQKB1R b", "c7c6", 20},
  {"rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/2N2N2/PP2PPPP/R1BQKB1R b", "c7c5", 15},
  // Same structure without White Nc3 yet: 1.d4 d5 2.Nf3 Nf6 3.c4 e6
  {"rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/5N2/PP2PPPP/RNBQKB1R w", "b1c3", 50},
  {"rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/5N2/PP2PPPP/RNBQKB1R w", "g2g3", 25},
  {"rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/5N2/PP2PPPP/RNBQKB1R w", "e2e3", 20},
  // Catalan: 1.d4 Nf6 2.Nf3 d5 3.c4 e6 4.g3 — develop, not ...h6
  {"rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/5NP1/PP2PP1P/RNBQKB1R b", "f8e7", 40},
  {"rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/5NP1/PP2PP1P/RNBQKB1R b", "d5c4", 30},
  {"rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/5NP1/PP2PP1P/RNBQKB1R b", "c7c5", 25},
  {"rnbqkb1r/pp3ppp/4pn2/2pp4/2PP4/2N2N2/PP2PPPP/R1BQKB1R w", "c4d5", 35},
  {"rnbqkb1r/pp3ppp/4pn2/2pp4/2PP4/2N2N2/PP2PPPP/R1BQKB1R w", "e2e3", 40},
  // After ...Be7 in QGD — castle / challenge center
  {"rnbqk2r/ppp1bppp/4pn2/3p4/2PP4/2N2N2/PP2PPPP/R1BQKB1R w", "c1g5", 40},
  {"rnbqk2r/ppp1bppp/4pn2/3p4/2PP4/2N2N2/PP2PPPP/R1BQKB1R w", "e2e3", 35},
  {"rnbqk2r/ppp1bppp/4pn2/3p4/2PP4/2N2N2/PP2PPPP/R1BQKB1R w", "c1f4", 15},
  {"rnbqk2r/ppp1bppp/4pn2/3p4/2PP4/2N2N2/PP2PPPP/R1BQKB1R b", "e8g8", 70},
  {"rnbqk2r/ppp1bppp/4pn2/3p4/2PP4/2N2N2/PP2PPPP/R1BQKB1R b", "c7c6", 20},

  // 1.e4 e5
  {"rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w", "g1f3", 70},
  {"rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w", "b1c3", 15},
  {"rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w", "f1c4", 10},

  // 1.e4 e5 2.Nf3
  {"rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b", "b8c6", 55},
  {"rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b", "g8f6", 30},
  {"rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b", "d7d6", 10},

  // 1.e4 e5 2.Nf3 Nc6
  {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w", "f1b5", 40},
  {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w", "f1c4", 35},
  {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w", "d2d4", 20},

  // Vienna / Three Knights: 1.e4 e5 2.Nc3 — classical replies
  {"rnbqkbnr/pppp1ppp/8/4p3/4P3/2N5/PPPP1PPP/R1BQKBNR b", "b8c6", 45},
  {"rnbqkbnr/pppp1ppp/8/4p3/4P3/2N5/PPPP1PPP/R1BQKBNR b", "g8f6", 40},
  {"rnbqkbnr/pppp1ppp/8/4p3/4P3/2N5/PPPP1PPP/R1BQKBNR b", "f8c5", 10},
  {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/2N5/PPPP1PPP/R1BQKBNR b", "g8f6", 55},
  {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/2N5/PPPP1PPP/R1BQKBNR b", "f8c5", 25},
  {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/2N5/PPPP1PPP/R1BQKBNR b", "f8b4", 15},
  {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/2N2N2/PPPP1PPP/R1BQKB1R b", "g8f6", 70},
  {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/2N2N2/PPPP1PPP/R1BQKB1R b", "f8b4", 20},
  {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/2N5/PPPP1PPP/R1BQKBNR w", "g1f3", 45},
  {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/2N5/PPPP1PPP/R1BQKBNR w", "f1c4", 30},
  {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/2N5/PPPP1PPP/R1BQKBNR w", "f2f4", 15},
  // Three Knights / 3...Bb4: prefer solid development over early Nxe5 adventures
  {"rnbqk2r/pppp1ppp/5n2/4p3/1b2P3/2N2N2/PPPP1PPP/R1BQKB1R w", "f1c4", 40},
  {"rnbqk2r/pppp1ppp/5n2/4p3/1b2P3/2N2N2/PPPP1PPP/R1BQKB1R w", "d2d3", 35},
  {"rnbqk2r/pppp1ppp/5n2/4p3/1b2P3/2N2N2/PPPP1PPP/R1BQKB1R w", "f3e5", 15},
  {"r1bqk2r/pppp1ppp/2n2n2/4p3/1b2P3/2N2N2/PPPP1PPP/R1BQKB1R w", "f1c4", 40},
  {"r1bqk2r/pppp1ppp/2n2n2/4p3/1b2P3/2N2N2/PPPP1PPP/R1BQKB1R w", "d2d3", 30},
  {"r1bqk2r/pppp1ppp/2n2n2/4p3/1b2P3/2N2N2/PPPP1PPP/R1BQKB1R w", "f3e5", 20},

  // Ruy Lopez: 1.e4 e5 2.Nf3 Nc6 3.Bb5
  {"r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b", "a7a6", 50},
  {"r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b", "g8f6", 35},
  {"r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b", "f8c5", 10},
  {"r1bqkbnr/1ppp1ppp/p1n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w", "b5a4", 70},
  {"r1bqkbnr/1ppp1ppp/p1n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w", "b5c6", 20},
  // Berlin / Classical — avoid Bd6 after 3...Nf6 4.Nc3
  {"r1bqkb1r/pppp1ppp/2n2n2/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w", "e1g1", 55},
  {"r1bqkb1r/pppp1ppp/2n2n2/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w", "d2d3", 20},
  {"r1bqkb1r/pppp1ppp/2n2n2/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w", "b1c3", 15},
  {"r1bqkb1r/pppp1ppp/2n2n2/1B2p3/4P3/2N2N2/PPPP1PPP/R1BQK2R b", "f8b4", 45},
  {"r1bqkb1r/pppp1ppp/2n2n2/1B2p3/4P3/2N2N2/PPPP1PPP/R1BQK2R b", "f8c5", 25},
  {"r1bqkb1r/pppp1ppp/2n2n2/1B2p3/4P3/2N2N2/PPPP1PPP/R1BQK2R b", "a7a6", 20},
  {"r1bqkb1r/pppp1ppp/2n2n2/1B2p3/4P3/2N2N2/PPPP1PPP/R1BQK2R b", "f8e7", 15},

  // Italian: 1.e4 e5 2.Nf3 Nc6 3.Bc4
  {"r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b", "g8f6", 45},
  {"r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b", "f8c5", 40},
  {"r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w", "d2d3", 40},
  {"r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w", "b1c3", 25},
  {"r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w", "d2d4", 20},
  {"r1bqk1nr/pppp1ppp/2n5/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w", "c2c3", 45},
  {"r1bqk1nr/pppp1ppp/2n5/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w", "d2d3", 30},
  {"r1bqk1nr/pppp1ppp/2n5/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w", "b1c3", 15},

  // Scotch: 1.e4 e5 2.Nf3 Nc6 3.d4
  {"r1bqkbnr/pppp1ppp/2n5/4p3/3PP3/5N2/PPP2PPP/RNBQKB1R b", "e5d4", 80},
  {"r1bqkbnr/pppp1ppp/2n5/8/3pP3/5N2/PPP2PPP/RNBQKB1R w", "f3d4", 80},
  // Scotch Gambit 4.Bc4 — Two Knights / solid development (avoid soft ...Bd7 lines)
  {"r1bqkbnr/pppp1ppp/2n5/8/2BpP3/5N2/PPP2PPP/RNBQK2R b", "g8f6", 55},
  {"r1bqkbnr/pppp1ppp/2n5/8/2BpP3/5N2/PPP2PPP/RNBQK2R b", "f8c5", 35},
  {"r1bqkb1r/pppp1ppp/2n2n2/4P3/2Bp4/5N2/PPP2PPP/RNBQK2R b", "d7d5", 85},
  {"r1bqkb1r/ppp2ppp/2n2n2/3pP3/2Bp4/5N2/PPP2PPP/RNBQK2R w", "e5f6", 40},
  {"r1bqkb1r/ppp2ppp/2n2n2/3pP3/2Bp4/5N2/PPP2PPP/RNBQK2R w", "c4b5", 45},
  // Scotch Gambit 5.O-O (no e5) — prefer ...Bc5 / ...Be7 over passive ...d6
  {"r1bqkb1r/pppp1ppp/2n2n2/8/2BpP3/5N2/PPP2PPP/RNBQ1RK1 b", "f8c5", 50},
  {"r1bqkb1r/pppp1ppp/2n2n2/8/2BpP3/5N2/PPP2PPP/RNBQ1RK1 b", "f8e7", 30},
  {"r1bqkb1r/pppp1ppp/2n2n2/8/2BpP3/5N2/PPP2PPP/RNBQ1RK1 b", "d7d6", 15},
  // After 6.Bb5 Ne4 7.O-O — prefer Be7 / a6 over Bd7
  {"r1bqkb1r/ppp2ppp/2n5/1B1pP3/3pn3/5N2/PPP2PPP/RNBQ1RK1 b", "f8e7", 45},
  {"r1bqkb1r/ppp2ppp/2n5/1B1pP3/3pn3/5N2/PPP2PPP/RNBQ1RK1 b", "a7a6", 30},
  {"r1bqkb1r/ppp2ppp/2n5/1B1pP3/3pn3/5N2/PPP2PPP/RNBQ1RK1 b", "c8d7", 20},

  // Petroff: 1.e4 e5 2.Nf3 Nf6 — prefer d4; Nxe5 lines lost Elo3000 g3
  {"rnbqkb1r/pppp1ppp/5n2/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w", "d2d4", 55},
  {"rnbqkb1r/pppp1ppp/5n2/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w", "b1c3", 25},
  {"rnbqkb1r/pppp1ppp/5n2/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w", "f3e5", 12},

  // Sicilian: 1.e4 c5
  {"rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w", "g1f3", 70},
  {"rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w", "b1c3", 12},
  {"rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w", "c2c3", 10},
  {"rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b", "d7d6", 35},
  {"rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b", "b8c6", 35},
  {"rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b", "e7e6", 25},
  {"rnbqkbnr/pp2pppp/3p4/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R w", "d2d4", 80},
  {"r1bqkbnr/pp1ppppp/2n5/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R w", "d2d4", 75},
  {"rnbqkbnr/pp1p1ppp/4p3/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R w", "d2d4", 75},
  // 2...d6 3.d4 Nf6 without ...cxd4 — do NOT grab on c5; develop
  {"rnbqkb1r/pp2pppp/3p1n2/2p5/3PP3/5N2/PPP2PPP/RNBQKB1R w", "b1c3", 70},
  {"rnbqkb1r/pp2pppp/3p1n2/2p5/3PP3/5N2/PPP2PPP/RNBQKB1R w", "d4c5", 5},
  {"rnbqkb1r/pp2pppp/3p1n2/2p5/3PP3/2N2N2/PPP2PPP/R1BQKB1R b", "c5d4", 75},
  {"rnbqkb1r/pp2pppp/3p1n2/2p5/3PP3/2N2N2/PPP2PPP/R1BQKB1R b", "e7e6", 15},
  // Avoid weak Bb5 Sicilians as White — prefer Open
  {"rnbqkbnr/pp1ppppp/8/2p5/4P3/2N5/PPPP1PPP/R1BQKBNR b", "b8c6", 40},
  {"rnbqkbnr/pp1ppppp/8/2p5/4P3/2N5/PPPP1PPP/R1BQKBNR b", "e7e6", 30},
  {"rnbqkbnr/pp1ppppp/8/2p5/4P3/2N5/PPPP1PPP/R1BQKBNR b", "d7d6", 25},
  {"r1bqkbnr/pp1ppppp/2n5/2p5/4P3/2N5/PPPP1PPP/R1BQKBNR w", "g1f3", 55},
  {"r1bqkbnr/pp1ppppp/2n5/2p5/4P3/2N5/PPPP1PPP/R1BQKBNR w", "f1b5", 15},
  {"r1bqkbnr/pp1ppppp/2n5/2p5/4P3/2N5/PPPP1PPP/R1BQKBNR w", "f2f4", 10},
  {"rnbqkbnr/pp1p1ppp/4p3/2p5/4P3/2N5/PPPP1PPP/R1BQKBNR w", "g1f3", 60},
  {"rnbqkbnr/pp1p1ppp/4p3/2p5/4P3/2N5/PPPP1PPP/R1BQKBNR w", "d2d4", 20},

  // Dutch: 1.d4 f5 — develop solidly
  {"rnbqkbnr/ppppp1pp/8/5p2/3P4/8/PPP1PPPP/RNBQKBNR w", "g1f3", 40},
  {"rnbqkbnr/ppppp1pp/8/5p2/3P4/8/PPP1PPPP/RNBQKBNR w", "c2c4", 35},
  {"rnbqkbnr/ppppp1pp/8/5p2/3P4/8/PPP1PPPP/RNBQKBNR w", "c1f4", 15},
  {"rnbqkb1r/ppppp1pp/5n2/5p2/3P4/5N2/PPP1PPPP/RNBQKB1R w", "c1f4", 35},
  {"rnbqkb1r/ppppp1pp/5n2/5p2/3P4/5N2/PPP1PPPP/RNBQKB1R w", "c2c4", 35},
  {"rnbqkb1r/ppppp1pp/5n2/5p2/3P4/5N2/PPP1PPPP/RNBQKB1R w", "g2g3", 20},

  // Italian / Two Knights quiet lines
  {"r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w", "d2d3", 45},
  {"r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w", "b1c3", 25},
  {"r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w", "d2d4", 15},
  {"r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/3P1N2/PPP2PPP/RNBQK2R b", "f8c5", 40},
  {"r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/3P1N2/PPP2PPP/RNBQK2R b", "f8e7", 30},
  {"r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/3P1N2/PPP2PPP/RNBQK2R b", "d7d6", 20},

  // Queen pawn: London / Jobava avoidance — prefer c4/Nf3
  {"rnbqkbnr/ppp1pppp/8/3p4/3P1B2/8/PPP1PPPP/RN1QKBNR b", "c7c5", 35},
  {"rnbqkbnr/ppp1pppp/8/3p4/3P1B2/8/PPP1PPPP/RN1QKBNR b", "g8f6", 35},
  {"rnbqkbnr/ppp1pppp/8/3p4/3P1B2/8/PPP1PPPP/RN1QKBNR b", "c8f5", 20},
  {"rnbqkb1r/ppp1pppp/5n2/3p4/3P1B2/8/PPP1PPPP/RN1QKBNR w", "e2e3", 50},
  {"rnbqkb1r/ppp1pppp/5n2/3p4/3P1B2/8/PPP1PPPP/RN1QKBNR w", "g1f3", 35},

  // French: 1.e4 e6
  {"rnbqkbnr/pppp1ppp/4p3/8/4P3/8/PPPP1PPP/RNBQKBNR w", "d2d4", 75},
  {"rnbqkbnr/pppp1ppp/4p3/8/3PP3/8/PPP2PPP/RNBQKBNR b", "d7d5", 85},
  {"rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/8/PPP2PPP/RNBQKBNR w", "b1c3", 40},
  {"rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/8/PPP2PPP/RNBQKBNR w", "e4e5", 35},
  {"rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/8/PPP2PPP/RNBQKBNR w", "e4d5", 15},
  // French as Black vs 3.Nc3 — Classical ...Nf6; avoid Winawer (v46 g2 collapse)
  {"rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/2N5/PPP2PPP/R1BQKBNR b", "g8f6", 70},
  {"rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/2N5/PPP2PPP/R1BQKBNR b", "d5e4", 20},
  {"rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/2N5/PPP2PPP/R1BQKBNR b", "f8b4", 8},
  {"rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/8/PPPN1PPP/R1BQKBNR b", "g8f6", 50},
  {"rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/8/PPPN1PPP/R1BQKBNR b", "c7c5", 35},
  // French Advance: 3.e5 — challenge with ...c5, develop Nf6 later
  {"rnbqkbnr/ppp2ppp/4p3/3pP3/3P4/8/PPP2PPP/RNBQKBNR b", "c7c5", 80},
  {"rnbqkbnr/pp3ppp/4p3/2ppP3/3P4/8/PPP2PPP/RNBQKBNR w", "c2c3", 60},
  {"rnbqkbnr/pp3ppp/4p3/2ppP3/3P4/8/PPP2PPP/RNBQKBNR w", "g1f3", 25},
  {"rnbqkbnr/pp3ppp/4p3/2ppP3/3P4/2P5/PP3PPP/RNBQKBNR b", "b8c6", 50},
  {"rnbqkbnr/pp3ppp/4p3/2ppP3/3P4/2P5/PP3PPP/RNBQKBNR b", "d8b6", 35},

  // Caro-Kann: 1.e4 c6
  {"rnbqkbnr/pp1ppppp/2p5/8/4P3/8/PPPP1PPP/RNBQKBNR w", "d2d4", 75},
  {"rnbqkbnr/pp1ppppp/2p5/8/3PP3/8/PPP2PPP/RNBQKBNR b", "d7d5", 85},
  {"rnbqkbnr/pp1ppppp/2p5/8/3PP3/8/PPP2PPP/RNBQKBNR b", "g7g6", 5},
  {"rnbqkbnr/pp2pppp/2p5/3p4/3PP3/8/PPP2PPP/RNBQKBNR w", "b1c3", 40},
  {"rnbqkbnr/pp2pppp/2p5/3p4/3PP3/8/PPP2PPP/RNBQKBNR w", "e4d5", 35},
  {"rnbqkbnr/pp2pppp/2p5/3p4/3PP3/8/PPP2PPP/RNBQKBNR w", "e4e5", 15},
  // Caro-Kann Modern / Gurgenidze: 1.e4 c6 2.d4 g6 — avoid soft Bc4 lines
  {"rnbqkbnr/pp1ppp1p/2p3p1/8/3PP3/8/PPP2PPP/RNBQKBNR w", "b1c3", 45},
  {"rnbqkbnr/pp1ppp1p/2p3p1/8/3PP3/8/PPP2PPP/RNBQKBNR w", "c2c3", 25},
  {"rnbqkbnr/pp1ppp1p/2p3p1/8/3PP3/8/PPP2PPP/RNBQKBNR w", "g1f3", 20},
  {"rnbqkbnr/pp1ppp1p/2p3p1/8/2BPP3/8/PPP2PPP/RNBQK1NR b", "d7d5", 40},
  {"rnbqkbnr/pp1ppp1p/2p3p1/8/2BPP3/8/PPP2PPP/RNBQK1NR b", "f8g7", 40},
  {"rnbqk1nr/pp1pppbp/2p3p1/8/2BPP3/2N5/PPP2PPP/R1BQK1NR b", "d7d6", 50},
  {"rnbqk1nr/pp1pppbp/2p3p1/8/2BPP3/2N5/PPP2PPP/R1BQK1NR b", "d7d5", 30},

  // Ruy Lopez mainline continuation
  {"r1bqkbnr/1ppp1ppp/p1n5/4p3/B3P3/5N2/PPPP1PPP/RNBQK2R b", "g8f6", 55},
  {"r1bqkbnr/1ppp1ppp/p1n5/4p3/B3P3/5N2/PPPP1PPP/RNBQK2R b", "b7b5", 30},
  {"r1bqkb1r/1ppp1ppp/p1n2n2/4p3/B3P3/5N2/PPPP1PPP/RNBQK2R w", "e1g1", 70},
  {"r1bqkb1r/1ppp1ppp/p1n2n2/4p3/B3P3/5N2/PPPP1PPP/RNBQK2R w", "d2d3", 20},

  // Sicilian Open after ...d6/Nc6/e6 xd4
  {"rnbqkbnr/pp2pppp/3p4/8/3pP3/5N2/PPP2PPP/RNBQKB1R w", "f3d4", 85},
  {"r1bqkbnr/pp1ppppp/2n5/8/3pP3/5N2/PPP2PPP/RNBQKB1R w", "f3d4", 85},
  {"rnbqkbnr/pp1p1ppp/4p3/8/3pP3/5N2/PPP2PPP/RNBQKB1R w", "f3d4", 85},
  {"rnbqkb1r/pp2pppp/3p1n2/8/3NP3/8/PPP2PPP/RNBQKB1R w", "b1c3", 70},
  {"r1bqkbnr/pp2pppp/2np4/8/3NP3/8/PPP2PPP/RNBQKB1R w", "b1c3", 55},
  {"r1bqkbnr/pp2pppp/2np4/8/3NP3/8/PPP2PPP/RNBQKB1R w", "c1e3", 25},

  // 1.d4 d5 — prefer QG/Nf3; do NOT play London as White (lost Elo3000 games)
  {"rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w", "c2c4", 58},
  {"rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w", "g1f3", 42},
  // 1.d4 d5 2.Nf3 — develop ...Nf6 (feeds London / QG lines)
  {"rnbqkbnr/ppp1pppp/8/3p4/3P4/5N2/PPP1PPPP/RNBQKB1R b", "g8f6", 70},
  {"rnbqkbnr/ppp1pppp/8/3p4/3P4/5N2/PPP1PPPP/RNBQKB1R b", "c7c6", 20},
  {"rnbqkbnr/ppp1pppp/8/3p4/3P4/5N2/PPP1PPPP/RNBQKB1R b", "e7e6", 10},
  // London as Black: 1.d4 d5 2.Nf3 Nf6 3.Bf4 — solid ...c5 / ...e6 / ...c6
  {"rnbqkb1r/ppp1pppp/5n2/3p4/3P1B2/5N2/PPP1PPPP/RN1QKB1R b", "c7c5", 45},
  {"rnbqkb1r/ppp1pppp/5n2/3p4/3P1B2/5N2/PPP1PPPP/RN1QKB1R b", "e7e6", 30},
  {"rnbqkb1r/ppp1pppp/5n2/3p4/3P1B2/5N2/PPP1PPPP/RN1QKB1R b", "c7c6", 20},
  {"rnbqkbnr/ppp1pppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR b", "e7e6", 35},
  {"rnbqkbnr/ppp1pppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR b", "c7c6", 35},
  {"rnbqkbnr/ppp1pppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR b", "d5c4", 20},
  {"rnbqkbnr/ppp2ppp/4p3/3p4/2PP4/8/PP2PPPP/RNBQKBNR w", "b1c3", 50},
  {"rnbqkbnr/ppp2ppp/4p3/3p4/2PP4/8/PP2PPPP/RNBQKBNR w", "g1f3", 30},
  {"rnbqkbnr/pp2pppp/2p5/3p4/2PP4/8/PP2PPPP/RNBQKBNR w", "b1c3", 40},
  {"rnbqkbnr/pp2pppp/2p5/3p4/2PP4/8/PP2PPPP/RNBQKBNR w", "g1f3", 35},
  {"rnbqkbnr/pp2pppp/2p5/3p4/2PP4/8/PP2PPPP/RNBQKBNR w", "c4d5", 15},
  // Exchange Slav: develop solidly (avoid early ...Nh5 / ...f6)
  {"rnbqkbnr/pp2pppp/2p5/3P4/3P4/8/PP2PPPP/RNBQKBNR b", "c6d5", 90},
  {"rnbqkbnr/pp2pppp/8/3p4/3P4/8/PP2PPPP/RNBQKBNR w", "b1c3", 50},
  {"rnbqkbnr/pp2pppp/8/3p4/3P4/8/PP2PPPP/RNBQKBNR w", "g1f3", 40},
  {"rnbqkbnr/pp2pppp/8/3p4/3P4/2N5/PP2PPPP/R1BQKBNR b", "g8f6", 70},
  {"rnbqkbnr/pp2pppp/8/3p4/3P4/2N5/PP2PPPP/R1BQKBNR b", "b8c6", 25},
  {"rnbqkb1r/pp2pppp/5n2/3p4/3P4/2N2N2/PP2PPPP/R1BQKB1R b", "b8c6", 45},
  {"rnbqkb1r/pp2pppp/5n2/3p4/3P4/2N2N2/PP2PPPP/R1BQKB1R b", "c8f5", 35},
  {"rnbqkb1r/pp2pppp/5n2/3p4/3P4/2N2N2/PP2PPPP/R1BQKB1R b", "e7e6", 20},
  {"r1bqkb1r/pp2pppp/2n2n2/3p4/3P1B2/2N2N2/PP2PPPP/R2QKB1R b", "c8f5", 45},
  {"r1bqkb1r/pp2pppp/2n2n2/3p4/3P1B2/2N2N2/PP2PPPP/R2QKB1R b", "e7e6", 35},
  {"r1bqkb1r/pp2pppp/2n2n2/3p4/3P1B2/2N2N2/PP2PPPP/R2QKB1R b", "c8g4", 20},

  // 1.d4 Nf6
  {"rnbqkb1r/pppppppp/5n2/8/3P4/8/PPP1PPPP/RNBQKBNR w", "c2c4", 50},
  {"rnbqkb1r/pppppppp/5n2/8/3P4/8/PPP1PPPP/RNBQKBNR w", "g1f3", 30},
  {"rnbqkb1r/pppppppp/5n2/8/3P4/8/PPP1PPPP/RNBQKBNR w", "c1g5", 10},
  {"rnbqkb1r/pppppppp/5n2/8/2PP4/8/PP2PPPP/RNBQKBNR b", "e7e6", 35},
  {"rnbqkb1r/pppppppp/5n2/8/2PP4/8/PP2PPPP/RNBQKBNR b", "g7g6", 30},
  {"rnbqkb1r/pppppppp/5n2/8/2PP4/8/PP2PPPP/RNBQKBNR b", "c7c5", 15},
  {"rnbqkb1r/pppppppp/5n2/8/2PP4/8/PP2PPPP/RNBQKBNR b", "e7e5", 8},
  {"rnbqkb1r/pppp1ppp/4pn2/8/2PP4/8/PP2PPPP/RNBQKBNR w", "b1c3", 50},
  {"rnbqkb1r/pppp1ppp/4pn2/8/2PP4/8/PP2PPPP/RNBQKBNR w", "g1f3", 30},
  {"rnbqkb1r/pppppp1p/5np1/8/2PP4/8/PP2PPPP/RNBQKBNR w", "b1c3", 45},
  {"rnbqkb1r/pppppp1p/5np1/8/2PP4/8/PP2PPPP/RNBQKBNR w", "g1f3", 30},

  // 1.Nf3 — solid replies (avoid early ...c5/...Bg4 traps)
  {"rnbqkbnr/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R b", "d7d5", 40},
  {"rnbqkbnr/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R b", "g8f6", 35},
  {"rnbqkbnr/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R b", "c7c5", 10},
  {"rnbqkb1r/pppppppp/5n2/8/8/5N2/PPPPPPPP/RNBQKB1R w", "c2c4", 40},
  {"rnbqkb1r/pppppppp/5n2/8/8/5N2/PPPPPPPP/RNBQKB1R w", "d2d4", 40},
  {"rnbqkb1r/pppppppp/5n2/8/2P5/5N2/PP1PPPPP/RNBQKB1R b", "e7e6", 35},
  {"rnbqkb1r/pppppppp/5n2/8/2P5/5N2/PP1PPPPP/RNBQKB1R b", "g7g6", 30},
  {"rnbqkb1r/pppppppp/5n2/8/2P5/5N2/PP1PPPPP/RNBQKB1R b", "c7c5", 15},
  {"rnbqkb1r/pppppppp/5n2/8/2P5/5N2/PP1PPPPP/RNBQKB1R b", "e7e5", 15},
  // Alapin: 1.e4 c5 2.c3 — prefer d4 classical setup as White; ...d5/...Nf6 as Black
  {"rnbqkbnr/pp1ppppp/8/2p5/4P3/2P5/PP1P1PPP/RNBQKBNR b", "d7d5", 45},
  {"rnbqkbnr/pp1ppppp/8/2p5/4P3/2P5/PP1P1PPP/RNBQKBNR b", "g8f6", 35},
  {"rnbqkbnr/pp1ppppp/8/2p5/4P3/2P5/PP1P1PPP/RNBQKBNR b", "e7e6", 15},
  {"rnbqkbnr/pp1ppppp/8/2p5/4P3/2P5/PP1P1PPP/RNBQKBNR w", "d2d4", 70},
  {"rnbqkbnr/pp1ppppp/8/2p5/4P3/2P5/PP1P1PPP/RNBQKBNR w", "g1f3", 25},

  // 1.e3 / 1.c3 — take the center
  {"rnbqkbnr/pppppppp/8/8/8/4P3/PPPP1PPP/RNBQKBNR b", "d7d5", 45},
  {"rnbqkbnr/pppppppp/8/8/8/4P3/PPPP1PPP/RNBQKBNR b", "e7e5", 35},
  {"rnbqkbnr/pppppppp/8/8/8/4P3/PPPP1PPP/RNBQKBNR b", "g8f6", 15},
  {"rnbqkbnr/pppppppp/8/8/8/2P5/PP1PPPPP/RNBQKBNR b", "d7d5", 40},
  {"rnbqkbnr/pppppppp/8/8/8/2P5/PP1PPPPP/RNBQKBNR b", "e7e5", 35},
  {"rnbqkbnr/pppppppp/8/8/8/2P5/PP1PPPPP/RNBQKBNR b", "g8f6", 20},

  // 1.a3 / other rare first moves — respond classically
  {"rnbqkbnr/pppppppp/8/8/8/P7/1PPPPPPP/RNBQKBNR b", "e7e5", 40},
  {"rnbqkbnr/pppppppp/8/8/8/P7/1PPPPPPP/RNBQKBNR b", "d7d5", 35},
  {"rnbqkbnr/pppppppp/8/8/8/P7/1PPPPPPP/RNBQKBNR b", "g8f6", 20},

  // Pirc / Modern structures after 1.d4 d6
  {"rnbqkbnr/ppp1pppp/3p4/8/3P4/8/PPP1PPPP/RNBQKBNR w", "c2c4", 35},
  {"rnbqkbnr/ppp1pppp/3p4/8/3P4/8/PPP1PPPP/RNBQKBNR w", "g1f3", 35},
  {"rnbqkbnr/ppp1pppp/3p4/8/3P4/8/PPP1PPPP/RNBQKBNR w", "e2e4", 25},
  {"rnbqkb1r/ppp1pppp/3p1n2/8/3PP3/8/PPP2PPP/RNBQKBNR w", "b1c3", 70},
  {"rnbqkb1r/ppp1pppp/3p1n2/8/3PP3/2N5/PPP2PPP/R1BQKBNR b", "g7g6", 50},
  {"rnbqkb1r/ppp1pppp/3p1n2/8/3PP3/2N5/PPP2PPP/R1BQKBNR b", "e7e5", 30},
  // Prefer keeping queens after ...e5 in Pirc-ish lines
  {"rnbqkb1r/ppp2ppp/3p1n2/4p3/3PP3/2N5/PPP2PPP/R1BQKBNR w", "d4e5", 25},
  {"rnbqkb1r/ppp2ppp/3p1n2/4p3/3PP3/2N5/PPP2PPP/R1BQKBNR w", "g1f3", 50},
  {"rnbqkb1r/ppp2ppp/3p1n2/4p3/3PP3/2N5/PPP2PPP/R1BQKBNR w", "d4d5", 20},

  // 1.c4
  {"rnbqkbnr/pppppppp/8/8/2P5/8/PP1PPPPP/RNBQKBNR b", "e7e5", 35},
  {"rnbqkbnr/pppppppp/8/8/2P5/8/PP1PPPPP/RNBQKBNR b", "g8f6", 30},
  {"rnbqkbnr/pppppppp/8/8/2P5/8/PP1PPPPP/RNBQKBNR b", "c7c5", 15},
  {"rnbqkbnr/pppppppp/8/8/2P5/8/PP1PPPPP/RNBQKBNR b", "e7e6", 15},
  {"rnbqkbnr/pppp1ppp/8/4p3/2P5/8/PP1PPPPP/RNBQKBNR w", "b1c3", 50},
  {"rnbqkbnr/pppp1ppp/8/4p3/2P5/8/PP1PPPPP/RNBQKBNR w", "g1f3", 30},
  {"rnbqkb1r/pppp1ppp/5n2/4p3/2P5/2N5/PP1PPPPP/R1BQKBNR b", "b8c6", 40},
  {"rnbqkb1r/pppp1ppp/5n2/4p3/2P5/2N5/PP1PPPPP/R1BQKBNR b", "f8b4", 35},
  {"rnbqkb1r/pppppppp/5n2/8/2P5/8/PP1PPPPP/RNBQKBNR w", "b1c3", 45},
  {"rnbqkb1r/pppppppp/5n2/8/2P5/8/PP1PPPPP/RNBQKBNR w", "g1f3", 35},
  {"rnbqkb1r/pppppppp/5n2/8/2P5/2N5/PP1PPPPP/R1BQKBNR b", "e7e6", 30},
  {"rnbqkb1r/pppppppp/5n2/8/2P5/2N5/PP1PPPPP/R1BQKBNR b", "g7g6", 25},
  {"rnbqkb1r/pppppppp/5n2/8/2P5/2N5/PP1PPPPP/R1BQKBNR b", "c7c5", 25},
};

std::string book_key(const Position& pos) {
  std::string f = pos.fen();
  auto sp1 = f.find(' ');
  if (sp1 == std::string::npos) return f;
  auto sp2 = f.find(' ', sp1 + 1);
  if (sp2 == std::string::npos) return f;
  return f.substr(0, sp2);
}

Move parse_uci_legal(const Position& pos, const std::string& u) {
  MoveListWrapper list(pos);
  for (const auto& em : list) {
    if (move_to_uci(em.move) == u) return em.move;
  }
  return MOVE_NONE;
}

} // namespace

Move probe_book(const Position& pos) {
  if (pos.game_ply() > 14) return MOVE_NONE;
  std::string key = book_key(pos);
  std::vector<std::pair<Move, int>> choices;
  int total = 0;
  for (const auto& e : kBook) {
    if (key == e.fen) {
      Move m = parse_uci_legal(pos, e.uci);
      if (m) {
        choices.push_back({m, e.weight});
        total += e.weight;
      }
    }
  }
  if (choices.empty() || total <= 0) return MOVE_NONE;
  static thread_local std::mt19937 rng{std::random_device{}()};
  std::uniform_int_distribution<int> dist(1, total);
  int r = dist(rng);
  for (auto& [m, w] : choices) {
    r -= w;
    if (r <= 0) return m;
  }
  return choices.back().first;
}

} // namespace ah
