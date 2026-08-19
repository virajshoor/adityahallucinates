#include "board/bitboard.hpp"
#include "board/zobrist.hpp"
#include "board/board.hpp"
#include "movegen/movegen.hpp"
#include "movegen/magics.hpp"
#include <iostream>
#include <cstdlib>

using namespace ah;

struct PerftCase {
  const char* fen;
  int depth;
  uint64_t nodes;
};

Key recompute_pawn_key(const Position& pos) {
  Key key = 0;
  Bitboard pawns = pos.pieces(PAWN);
  while (pawns) {
    Square sq = pop_lsb(pawns);
    key ^= Zobrist::psq[pos.piece_on(sq)][sq];
  }
  return key;
}

bool pawn_key_tests() {
  struct Case {
    const char* fen;
    Move move;
  };
  const Case cases[] = {
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
     Move(SQ_E2, SQ_E4)},
    {"4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
     Move(SQ_E5, SQ_D6, EN_PASSANT)},
    {"4k3/P7/8/8/8/8/8/4K2k w - - 0 1",
     Move(SQ_A7, SQ_A8, PROMOTION, QUEEN)},
  };

  for (const auto& c : cases) {
    Position pos;
    StateInfo states[2];
    pos.set(c.fen, states[0]);
    const Key initial = pos.pawn_key();
    if (initial != recompute_pawn_key(pos) || !pos.is_legal(c.move))
      return false;
    pos.do_move(c.move, states[1]);
    if (pos.pawn_key() != recompute_pawn_key(pos))
      return false;
    pos.undo_move(c.move);
    if (pos.pawn_key() != initial || pos.pawn_key() != recompute_pawn_key(pos))
      return false;
  }
  return true;
}

int main() {
  Bitboards::init();
  Zobrist::init();
  Magics::init();

  if (!pawn_key_tests()) {
    std::cerr << "pawn-key incremental test failed\n";
    return 1;
  }

  PerftCase cases[] = {
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 1, 20},
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 2, 400},
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 3, 8902},
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4, 197281},
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609},
    // Kiwipete
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 1, 48},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 2, 2039},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3, 97862},
    // Position 3
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 1, 14},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 2, 191},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 3, 2812},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4, 43238},
    // Position 4
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 1, 6},
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 2, 264},
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3, 9467},
    // Position 5
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 1, 44},
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 2, 1486},
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3, 62379},
    // Position 6
    {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 1, 46},
    {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 2, 2079},
    {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 3, 89890},
  };

  int failed = 0;
  for (const auto& c : cases) {
    // Skip very deep for quick CI unless env FULL_PERFT=1
    if (c.depth >= 5 && !std::getenv("FULL_PERFT")) continue;
    Position pos;
    StateInfo si;
    pos.set(c.fen, si);
    uint64_t n = perft(pos, c.depth);
    bool ok = n == c.nodes;
    std::cout << (ok ? "OK  " : "FAIL") << " depth " << c.depth
              << " got " << n << " expected " << c.nodes
              << " fen=" << c.fen << "\n";
    if (!ok) ++failed;
  }

  if (failed) {
    std::cerr << failed << " perft cases failed\n";
    return 1;
  }
  std::cout << "All perft tests passed\n";
  return 0;
}
