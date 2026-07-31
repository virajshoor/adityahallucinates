#pragma once

#include "board/board.hpp"
#include <string>
#include <cstdint>

namespace ah {

// Dual-perspective int16 accumulator for incremental updates.
struct NnueAccumulator {
  static constexpr int MAX_H1 = 256;
  int16_t acc[COLOR_NB][MAX_H1]{};
  bool computed = false;
};

struct NnueNet {
  bool loaded = false;
  bool load(const std::string& path);
  Value evaluate(const Position& pos) const;

  // Incremental API (perspective = WHITE/BLACK king view)
  void refresh(NnueAccumulator& a, const Position& pos) const;
  void put_piece(NnueAccumulator& a, Piece pc, Square sq) const;
  void remove_piece(NnueAccumulator& a, Piece pc, Square sq) const;
  Value evaluate_acc(const NnueAccumulator& a, Color stm) const;
};

NnueNet& nnue();
bool nnue_ready();
bool load_nnue(const std::string& path);

} // namespace ah
