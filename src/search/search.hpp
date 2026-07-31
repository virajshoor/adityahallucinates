#pragma once

#include "board/board.hpp"
#include "search/tt.hpp"
#include <atomic>
#include <string>
#include <vector>
#include <cstdint>

namespace ah {

struct SearchLimits {
  int depth = 0;
  int movetime = 0;
  int wtime = 0, btime = 0, winc = 0, binc = 0;
  int movestogo = 0;
  bool infinite = false;
  std::vector<Move> searchmoves;
};

struct SearchInfo {
  std::atomic<bool> stop{false};
  uint64_t nodes = 0;
  int seldepth = 0;
};

class Search {
public:
  struct Stack {
    Move* pv = nullptr;
    Move killers[2] = {};
    Move current = MOVE_NONE;
    Move excluded = MOVE_NONE;
    int ply = 0;
    int staticEval = VALUE_NONE;
    int moveCount = 0;
  };

  Search();
  void set_hash(size_t mb) { tt.resize(mb); }
  void clear();

  Move think(Position& pos, const SearchLimits& limits);
  uint64_t nodes() const { return info.nodes; }
  void request_stop() { info.stop = true; }

  TranspositionTable tt;
  SearchInfo info;

private:
  Value search_node(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode);
  Value qsearch(Position& pos, Stack* ss, Value alpha, Value beta);
  void update_pv(Stack* ss, Move m);
  void order_moves(Position& pos, ExtMove* begin, ExtMove* end, Move ttMove, Stack* ss);
  bool time_up() const;
  int64_t now_ms() const;

  static constexpr int MAX_PV = MAX_PLY + 1;
  Move pv_table[MAX_PLY + 1][MAX_PV]{};
  int history[COLOR_NB][64][64]{};
  int captureHistory[PIECE_NB][64][PIECE_TYPE_NB]{};
  Move countermove[PIECE_NB][64]{};

  Move bestRootMove = MOVE_NONE;
  int64_t startTime = 0;
  int64_t allocatedTime = 0;
  SearchLimits limits{};
};

} // namespace ah
