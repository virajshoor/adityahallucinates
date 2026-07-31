#include "search/book.hpp"
#include "movegen/movegen.hpp"
#include <random>
#include <unordered_map>
#include <string>

namespace ah {

namespace {

struct BookEntry {
  const char* fen; // side-to-move position key via fen pieces+stm+castling+ep only
  const char* uci;
  int weight;
};

// Compact book: common healthy developing moves
const BookEntry kBook[] = {
  // Start
  {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w", "e2e4", 50},
  {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w", "d2d4", 40},
  {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w", "c2c4", 15},
  {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w", "g1f3", 15},

  // After 1.e4
  {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b", "e7e5", 40},
  {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b", "c7c5", 35},
  {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b", "e7e6", 15},
  {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b", "c7c6", 10},

  // After 1.d4
  {"rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b", "d7d5", 40},
  {"rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b", "g8f6", 35},
  {"rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b", "e7e6", 10},

  // 1.e4 e5
  {"rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w", "g1f3", 60},
  {"rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w", "b1c3", 20},
  {"rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w", "f1c4", 15},

  // 1.e4 e5 2.Nf3 Nc6
  {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w", "f1b5", 35},
  {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w", "f1c4", 35},
  {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w", "d2d4", 20},

  // 1.e4 c5
  {"rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w", "g1f3", 55},
  {"rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w", "b1c3", 20},
  {"rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w", "c2c3", 15},

  // 1.d4 d5
  {"rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w", "c2c4", 45},
  {"rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w", "g1f3", 25},
  {"rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w", "c1f4", 15},

  // 1.d4 Nf6
  {"rnbqkb1r/pppppppp/5n2/8/3P4/8/PPP1PPPP/RNBQKBNR w", "c2c4", 45},
  {"rnbqkb1r/pppppppp/5n2/8/3P4/8/PPP1PPPP/RNBQKBNR w", "g1f3", 25},
};

std::string book_key(const Position& pos) {
  // piece placement + side to move only (ignore castling/ep/counters)
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
  if (pos.game_ply() > 8) return MOVE_NONE;
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
