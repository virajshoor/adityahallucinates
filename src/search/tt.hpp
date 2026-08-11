#pragma once

#include "types.hpp"
#include <vector>
#include <cstring>
#include <algorithm>
#include <atomic>

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

// Shared across Lazy SMP threads. Stores publish `key` last; probes re-check key
// after reading payload (lockless, rare torn reads are discarded).
class TranspositionTable {
public:
  static constexpr size_t ClusterSize = 3;

  void resize(size_t mb) {
    size_t bytes = mb * 1024ULL * 1024ULL;
    size_t entries = std::max<size_t>(ClusterSize, bytes / sizeof(TTEntry));
    size_ = std::max<size_t>(1, entries / ClusterSize);
    table_.assign(size_ * ClusterSize, {});
    age_.store(0, std::memory_order_relaxed);
  }

  void clear() {
    std::memset(table_.data(), 0, table_.size() * sizeof(TTEntry));
    age_.store(0, std::memory_order_relaxed);
  }

  void new_search() {
    age_.store(uint8_t(age_.load(std::memory_order_relaxed) + 1), std::memory_order_relaxed);
  }

  TTEntry* probe(Key key, bool& hit) {
    size_t idx = (size_t)((__uint128_t(key) * __uint128_t(size_)) >> 64);
    TTEntry* cluster = &table_[idx * ClusterSize];
    const uint8_t age = age_.load(std::memory_order_relaxed);
    for (size_t i = 0; i < ClusterSize; ++i) {
      // Read key first; re-validate after copying payload fields.
      Key k = cluster[i].key;
      if (k == key) {
        TTEntry snap = cluster[i];
        if (snap.key == key) {
          hit = true;
          cluster[i].age = age;
          // Return live pointer — callers only read after hit validation above.
          return &cluster[i];
        }
      }
    }
    hit = false;
    TTEntry* replace = &cluster[0];
    for (size_t i = 1; i < ClusterSize; ++i) {
      if (cluster[i].key == 0) { replace = &cluster[i]; break; }
      auto score_slot = [&](const TTEntry& e) {
        int ageDist = uint8_t(age - e.age);
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
    const uint8_t age = age_.load(std::memory_order_relaxed);
    if (e->key != key || depth + 2 >= e->depth || flag == TT_EXACT || e->age != age) {
      e->move = move ? move : e->move;
      e->score = int16_t(score);
      e->eval = int16_t(eval);
      e->depth = uint8_t(std::max(0, depth));
      e->flag = flag;
      e->age = age;
      // Publish last so concurrent probes never see a half-written entry with matching key.
      std::atomic_thread_fence(std::memory_order_release);
      e->key = key;
    } else if (move) {
      e->move = move;
    }
  }

private:
  std::vector<TTEntry> table_;
  size_t size_ = 0;
  std::atomic<uint8_t> age_{0};
};

} // namespace ah
