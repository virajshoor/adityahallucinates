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
    // Keep full cluster count (not floored to power-of-two) for ~full Hash MB usage.
    size_ = std::max<size_t>(1, entries / ClusterSize);
    table_.assign(size_ * ClusterSize, {});
    age_ = 0;
  }

  void clear() {
    std::memset(table_.data(), 0, table_.size() * sizeof(TTEntry));
    age_ = 0;
  }

  void new_search() { age_ = uint8_t(age_ + 1); }

  TTEntry* probe(Key key, bool& hit) {
    // Multiply-high index into arbitrary cluster count
    size_t idx = (size_t)((__uint128_t(key) * __uint128_t(size_)) >> 64);
    TTEntry* cluster = &table_[idx * ClusterSize];
    for (size_t i = 0; i < ClusterSize; ++i) {
      if (cluster[i].key == key) {
        hit = true;
        cluster[i].age = age_;
        return &cluster[i];
      }
    }
    hit = false;
    // Prefer empty, then generation-distance + shallower depth; protect exact entries.
    TTEntry* replace = &cluster[0];
    for (size_t i = 1; i < ClusterSize; ++i) {
      if (cluster[i].key == 0) { replace = &cluster[i]; break; }
      auto score_slot = [&](const TTEntry& e) {
        int ageDist = uint8_t(age_ - e.age);
        int protect = (e.flag == TT_EXACT ? 64 : 0) + int(e.depth);
        return protect - 4 * ageDist;
      };
      if (score_slot(cluster[i]) < score_slot(*replace)) replace = &cluster[i];
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
