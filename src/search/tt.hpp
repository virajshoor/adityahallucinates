#pragma once

#include "types.hpp"
#include <vector>
#include <cstring>
#include <algorithm>

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
  static constexpr size_t ClusterSize = 3;

  void resize(size_t mb) {
    size_t bytes = mb * 1024ULL * 1024ULL;
    size_t entries = std::max<size_t>(ClusterSize, bytes / sizeof(TTEntry));
    // number of clusters as power of two
    size_t clusters = entries / ClusterSize;
    size_t pow2 = 1;
    while (pow2 * 2 <= clusters) pow2 *= 2;
    size_ = pow2;
    table_.assign(size_ * ClusterSize, {});
    age_ = 0;
  }

  void clear() {
    std::memset(table_.data(), 0, table_.size() * sizeof(TTEntry));
    age_ = 0;
  }

  void new_search() { age_ = uint8_t(age_ + 1); }

  TTEntry* probe(Key key, bool& hit) {
    TTEntry* cluster = &table_[(key & (size_ - 1)) * ClusterSize];
    for (size_t i = 0; i < ClusterSize; ++i) {
      if (cluster[i].key == key) {
        hit = true;
        cluster[i].age = age_;
        return &cluster[i];
      }
    }
    hit = false;
    // Return replaceable slot: prefer empty, then oldest/shallowest
    TTEntry* replace = &cluster[0];
    for (size_t i = 1; i < ClusterSize; ++i) {
      if (cluster[i].key == 0) { replace = &cluster[i]; break; }
      int ri = (replace->age == age_ ? 256 : 0) + replace->depth;
      int ci = (cluster[i].age == age_ ? 256 : 0) + cluster[i].depth;
      if (ci < ri) replace = &cluster[i];
    }
    return replace;
  }

  void store(Key key, Depth depth, Value score, TTFlag flag, Move move, Value eval) {
    bool hit = false;
    TTEntry* e = probe(key, hit);
    // Always overwrite empty / same key / deeper / exact / older generation
    if (e->key != key || depth + 2 >= e->depth || flag == TT_EXACT || e->age != age_) {
      e->key = key;
      e->depth = uint8_t(std::max(0, depth));
      e->score = int16_t(score);
      e->eval = int16_t(eval);
      e->flag = flag;
      e->age = age_;
      if (move) e->move = move;
    } else if (move) {
      e->move = move;
    }
  }

private:
  std::vector<TTEntry> table_;
  size_t size_ = 0; // cluster count
  uint8_t age_ = 0;
};

} // namespace ah
