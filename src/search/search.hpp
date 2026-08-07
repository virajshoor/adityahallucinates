#pragma once

#include "board/board.hpp"
#include "search/tt.hpp"
#include "nnue/nnue.hpp"
#include <atomic>
#include <memory>
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
  std::atomic<uint64_t> nodes{0};
  std::atomic<int> seldepth{0};
};

class Search {
public:
  struct Stack {
    Move* pv = nullptr;
    Move killers[2] = {};
    Move current = MOVE_NONE;
    Piece movedPiece = NO_PIECE;
    int ply = 0;
    int staticEval = VALUE_NONE;
    NnueAccumulator acc{};
  };

  Search();
  explicit Search(std::shared_ptr<TranspositionTable> sharedTt, SearchInfo* sharedInfo);
  void set_hash(size_t mb) { tt->resize(mb); }
  void set_threads(int n);
  int threads() const { return numThreads; }
  void clear();

  Move think(Position& pos, const SearchLimits& limits);
  uint64_t nodes() const { return info->nodes.load(std::memory_order_relaxed); }
  void request_stop() { info->stop.store(true, std::memory_order_relaxed); }

  std::shared_ptr<TranspositionTable> tt;
  SearchInfo ownedInfo;
  SearchInfo* info = &ownedInfo;

private:
  Value search_node(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode);
  Value qsearch(Position& pos, Stack* ss, Value alpha, Value beta);
  void update_pv(Stack* ss, Move m);
  void order_moves(Position& pos, ExtMove* begin, ExtMove* end, Move ttMove, Stack* ss);
  bool time_up() const;
  int64_t now_ms() const;
  Value eval_pos(const Position& pos, Stack* ss) const;
  void iterative_deepening(Position& pos, bool emitInfo, int startDepth = 1, int aspBase = 28);
  void helper_loop(const std::string& fen, int helperId);

  static constexpr int MAX_PV = MAX_PLY + 1;
  Move pv_table[MAX_PLY + 1][MAX_PV]{};
  int history[COLOR_NB][64][64]{};
  int captureHistory[PIECE_NB][64][PIECE_TYPE_NB]{};
  int contHistory[PIECE_NB][64][64]{};
  Move countermove[PIECE_NB][64]{};

  Move bestRootMove = MOVE_NONE;
  int64_t startTime = 0;
  int64_t allocatedTime = 0;
  int64_t hardDeadline = 0;
  SearchLimits limits{};
  bool useNnueAcc = false;
  bool silent = false;
  int numThreads = 1;
};

} // namespace ah
