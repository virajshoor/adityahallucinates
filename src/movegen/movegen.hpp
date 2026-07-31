#pragma once

#include "board/board.hpp"

namespace ah {

enum GenType { CAPTURES, QUIETS, EVASIONS, NON_EVASIONS, LEGAL };

template <GenType Type>
ExtMove* generate(const Position& pos, ExtMove* moveList);

inline ExtMove* generate_legal(const Position& pos, ExtMove* moveList) {
  return generate<LEGAL>(pos, moveList);
}

struct MoveListWrapper {
  explicit MoveListWrapper(const Position& pos) {
    last = generate_legal(pos, moves.data());
  }
  const ExtMove* begin() const { return moves.data(); }
  const ExtMove* end() const { return last; }
  size_t size() const { return last - moves.data(); }
  bool contains(Move m) const {
    for (auto& e : *this) if (e.move == m) return true;
    return false;
  }
  MoveList moves{};
  ExtMove* last;
};

uint64_t perft(Position& pos, Depth depth);

} // namespace ah
