#pragma once

#include "types.hpp"
#include <vector>
#include <cstring>

namespace ah {

enum TTFlag : uint8_t { TT_NONE, TT_EXACT, TT_LOWER, TT_UPPER };

struct TTEntry {
  Key key = 0;
  Move move = MOVE_NONE;
  int16_t score = 0;
  int16_t eval = 0;
  uint8_t depth = 0;
  uint8_t flag = TT_NONE;
  uint8_t age = 0;
};

class TranspositionTable {
public:
  void resize(size_t mb) {
    size_t bytes = mb * 1024ULL * 1024ULL;
    size_ = std::max<size_t>(1, bytes / sizeof(TTEntry));
    // cluster as power of two
    size_t pow2 = 1;
    while (pow2 * 2 <= size_) pow2 *= 2;
    size_ = pow2;
    table_.assign(size_, {});
    age_ = 0;
  }

  void clear() {
    std::memset(table_.data(), 0, table_.size() * sizeof(TTEntry));
    age_ = 0;
  }

  void new_search() { age_ = uint8_t(age_ + 1); }

  TTEntry* probe(Key key, bool& hit) {
    TTEntry* e = &table_[key & (size_ - 1)];
    hit = (e->key == key);
    return e;
  }

  void store(Key key, Depth depth, Value score, TTFlag flag, Move move, Value eval) {
    TTEntry* e = &table_[key & (size_ - 1)];
    if (e->key != key || depth + 2 >= e->depth || flag == TT_EXACT || e->age != age_) {
      e->key = key;
      e->depth = uint8_t(std::max(0, depth));
      e->score = int16_t(score);
      e->eval = int16_t(eval);
      e->flag = flag;
      e->age = age_;
      if (move) e->move = move;
    }
  }

private:
  std::vector<TTEntry> table_;
  size_t size_ = 0;
  uint8_t age_ = 0;
};

} // namespace ah
