#pragma once

#include "board/board.hpp"
#include <string>
#include <cstdint>
#include <cstring>

namespace ah {

// Dual-perspective int16 accumulator for incremental updates.
struct NnueAccumulator {
  static constexpr int MAX_H1 = 256;
  int16_t acc[COLOR_NB][MAX_H1]{};
  // Absolute king squares used for HalfKA feature buckets (AHNNUEF4).
  Square ksq[COLOR_NB]{SQ_NONE, SQ_NONE};
  bool computed = false;

  void copy_from(const NnueAccumulator& o) {
    std::memcpy(this, &o, sizeof(NnueAccumulator));
  }
};

struct NnueNet {
  bool loaded = false;
  bool load(const std::string& path);
  Value evaluate(const Position& pos) const;
  Value evaluate(const Position& pos, const NnueAccumulator& a) const;

  void refresh(NnueAccumulator& a, const Position& pos) const;
  void put_piece(NnueAccumulator& a, Piece pc, Square sq) const;
  void remove_piece(NnueAccumulator& a, Piece pc, Square sq) const;
  // Apply move to child accumulator (pos = position BEFORE move)
  void do_move(NnueAccumulator& child, const NnueAccumulator& parent,
               const Position& pos, Move m) const;
  Value evaluate_acc(const NnueAccumulator& a, Color stm) const;
};

NnueNet& nnue();
bool nnue_ready();
bool load_nnue(const std::string& path);

} // namespace ah
