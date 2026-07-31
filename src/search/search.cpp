#include "search/search.hpp"
#include "eval/eval.hpp"
#include "movegen/movegen.hpp"
#include <chrono>
#include <algorithm>
#include <iostream>
#include <cmath>

namespace ah {

namespace {
constexpr int FutilityMargin = 150;
constexpr int RazorMargin = 300;
}

Search::Search() {
  tt.resize(64);
  clear();
}

void Search::clear() {
  tt.clear();
  std::memset(history, 0, sizeof(history));
  std::memset(pv_table, 0, sizeof(pv_table));
  info.nodes = 0;
  info.seldepth = 0;
  info.stop = false;
}

int64_t Search::now_ms() const {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool Search::time_up() const {
  if (info.stop) return true;
  if (limits.infinite || limits.depth) return false;
  if (allocatedTime <= 0) return false;
  return (now_ms() - startTime) >= allocatedTime;
}

void Search::update_pv(Stack* ss, Move m) {
  Move* pv = ss->pv;
  pv[0] = m;
  const Move* child = (ss + 1)->pv;
  int i = 0;
  while (child[i]) {
    pv[i + 1] = child[i];
    ++i;
  }
  pv[i + 1] = MOVE_NONE;
}

void Search::order_moves(Position& pos, ExtMove* begin, ExtMove* end, Move ttMove, Stack* ss) {
  for (ExtMove* m = begin; m != end; ++m) {
    Move mv = m->move;
    if (mv == ttMove) {
      m->score = 2000000;
    } else if (pos.piece_on(mv.to()) || mv.type() == EN_PASSANT) {
      int victim = mv.type() == EN_PASSANT ? PAWN : type_of(pos.piece_on(mv.to()));
      int attacker = type_of(pos.piece_on(mv.from()));
      m->score = 1000000 + victim * 16 - attacker;
      if (mv.type() == PROMOTION) m->score += 500 + mv.promotion_type() * 10;
    } else if (mv == ss->killers[0]) {
      m->score = 900000;
    } else if (mv == ss->killers[1]) {
      m->score = 800000;
    } else {
      m->score = history[pos.side_to_move()][mv.from()][mv.to()];
      if (mv.type() == PROMOTION) m->score += 50000 + mv.promotion_type() * 1000;
    }
  }
  std::stable_sort(begin, end);
}

Value Search::qsearch(Position& pos, Stack* ss, Value alpha, Value beta) {
  ++info.nodes;
  if ((info.nodes & 2047) == 0 && time_up()) {
    info.stop = true;
    return 0;
  }

  ss->pv[0] = MOVE_NONE;
  if (ss->ply >= MAX_PLY - 1) return evaluate(pos);

  Value stand = evaluate(pos);
  if (stand >= beta) return stand;
  if (stand > alpha) alpha = stand;

  ExtMove moves[MAX_MOVES];
  ExtMove* end;
  if (pos.checkers())
    end = generate<LEGAL>(pos, moves);
  else
    end = generate<CAPTURES>(pos, moves);

  // Filter captures to legal when not in check
  if (!pos.checkers()) {
    ExtMove* n = moves;
    for (ExtMove* m = moves; m != end; ++m)
      if (pos.is_legal(m->move)) *n++ = *m;
    end = n;
  }

  order_moves(pos, moves, end, MOVE_NONE, ss);
  StateInfo st;

  for (ExtMove* em = moves; em != end; ++em) {
    Move m = em->move;
    if (!pos.checkers() && !pos.see_ge(m, 0)) continue;

    pos.do_move(m, st);
    Value score = -qsearch(pos, ss + 1, -beta, -alpha);
    pos.undo_move(m);
    if (info.stop) return 0;

    if (score > alpha) {
      alpha = score;
      update_pv(ss, m);
      if (score >= beta) return score;
    }
  }
  return alpha;
}

Value Search::search_node(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode) {
  const bool rootNode = (ss->ply == 0);
  const bool pvNode = (beta - alpha) > 1;
  ++info.nodes;

  if ((info.nodes & 2047) == 0 && time_up()) {
    info.stop = true;
    return 0;
  }

  ss->pv[0] = MOVE_NONE;
  (ss + 1)->ply = ss->ply + 1;
  (ss + 1)->killers[0] = (ss + 1)->killers[1] = MOVE_NONE;

  if (!rootNode) {
    if (pos.is_draw(ss->ply) || ss->ply >= MAX_PLY - 1)
      return VALUE_DRAW;
  }

  // Mate distance pruning
  alpha = std::max(alpha, mated_in(ss->ply));
  beta = std::min(beta, mate_in(ss->ply + 1));
  if (alpha >= beta) return alpha;

  if (depth <= 0)
    return qsearch(pos, ss, alpha, beta);

  info.seldepth = std::max(info.seldepth, ss->ply);

  bool ttHit = false;
  TTEntry* tte = tt.probe(pos.key(), ttHit);
  Move ttMove = (ttHit ? tte->move : MOVE_NONE);
  Value ttValue = ttHit ? Value(tte->score) : VALUE_NONE;

  if (!pvNode && ttHit && tte->depth >= depth) {
    if (tte->flag == TT_EXACT) return ttValue;
    if (tte->flag == TT_LOWER && ttValue >= beta) return ttValue;
    if (tte->flag == TT_UPPER && ttValue <= alpha) return ttValue;
  }

  Value eval;
  const bool inCheck = pos.checkers();
  if (inCheck) {
    eval = ss->staticEval = VALUE_NONE;
  } else {
    eval = ss->staticEval = (ttHit ? Value(tte->eval) : evaluate(pos));
    if (!ttHit) tt.store(pos.key(), 0, VALUE_NONE, TT_NONE, MOVE_NONE, eval);
  }

  // Razoring
  if (!pvNode && !inCheck && depth <= 3 && eval + RazorMargin * depth < alpha)
    return qsearch(pos, ss, alpha, beta);

  // Null move pruning
  if (!pvNode && !inCheck && depth >= 3 && eval >= beta &&
      pos.non_pawn_material(pos.side_to_move()) && ss->ply > 0) {
    StateInfo st;
    int R = 3 + depth / 4;
    pos.do_null_move(st);
    Value nullScore = -search_node(pos, ss + 1, -beta, -beta + 1, depth - R, !cutNode);
    pos.undo_null_move();
    if (info.stop) return 0;
    if (nullScore >= beta)
      return nullScore >= VALUE_MATE_IN_MAX_PLY ? beta : nullScore;
  }

  ExtMove moves[MAX_MOVES];
  ExtMove* end = generate<LEGAL>(pos, moves);
  if (moves == end) {
    return inCheck ? mated_in(ss->ply) : VALUE_DRAW;
  }

  order_moves(pos, moves, end, ttMove, ss);

  Move bestMove = MOVE_NONE;
  Value bestScore = -VALUE_INFINITE;
  int moveCount = 0;
  StateInfo st;
  TTFlag flag = TT_UPPER;

  for (ExtMove* em = moves; em != end; ++em) {
    Move m = em->move;
    if (rootNode && !limits.searchmoves.empty()) {
      bool found = false;
      for (Move sm : limits.searchmoves) if (sm == m) found = true;
      if (!found) continue;
    }

    ++moveCount;
    ss->current = m;
    bool givesCheck = pos.gives_check(m);
    bool capture = pos.piece_on(m.to()) || m.type() == EN_PASSANT || m.type() == PROMOTION;

    // Futility
    if (!pvNode && !inCheck && !capture && !givesCheck && depth <= 5 &&
        eval + FutilityMargin * depth <= alpha && moveCount > 1)
      continue;

    // Late move pruning
    if (!pvNode && !capture && !givesCheck && depth <= 4 && moveCount > 3 + depth * depth)
      continue;

    Depth newDepth = depth - 1;
    int extension = 0;
    if (givesCheck && pos.see_ge(m)) extension = 1;

    // LMR
    Depth reduction = 0;
    if (depth >= 3 && moveCount > 2 + 2 * pvNode && !capture && !givesCheck) {
      reduction = 1;
      if (moveCount > 6) ++reduction;
      if (cutNode) ++reduction;
      if (ss->killers[0] == m || ss->killers[1] == m) reduction = std::max(0, reduction - 1);
      reduction = std::min(reduction, newDepth - 1);
    }

    pos.do_move(m, st);
    Value score;
    if (moveCount == 1) {
      score = -search_node(pos, ss + 1, -beta, -alpha, newDepth + extension, false);
    } else {
      score = -search_node(pos, ss + 1, -alpha - 1, -alpha, newDepth + extension - reduction, true);
      if (reduction && score > alpha)
        score = -search_node(pos, ss + 1, -alpha - 1, -alpha, newDepth + extension, !cutNode);
      if (score > alpha && score < beta)
        score = -search_node(pos, ss + 1, -beta, -alpha, newDepth + extension, false);
    }
    pos.undo_move(m);
    if (info.stop) return 0;

    if (score > bestScore) {
      bestScore = score;
      if (score > alpha) {
        bestMove = m;
        alpha = score;
        flag = TT_EXACT;
        update_pv(ss, m);
        if (score >= beta) {
          flag = TT_LOWER;
          if (!capture) {
            if (ss->killers[0] != m) {
              ss->killers[1] = ss->killers[0];
              ss->killers[0] = m;
            }
            history[pos.side_to_move()][m.from()][m.to()] += depth * depth;
          }
          break;
        }
      }
    }
  }

  tt.store(pos.key(), depth, bestScore, flag, bestMove ? bestMove : ttMove, ss->staticEval);
  return bestScore;
}

Move Search::think(Position& pos, const SearchLimits& lim) {
  limits = lim;
  info.stop = false;
  info.nodes = 0;
  info.seldepth = 0;
  tt.new_search();
  bestRootMove = MOVE_NONE;

  startTime = now_ms();
  allocatedTime = 0;
  if (limits.movetime > 0) {
    allocatedTime = limits.movetime - 10;
  } else if (limits.wtime || limits.btime) {
    int time = pos.side_to_move() == WHITE ? limits.wtime : limits.btime;
    int inc = pos.side_to_move() == WHITE ? limits.winc : limits.binc;
    int mtg = limits.movestogo > 0 ? limits.movestogo : 30;
    allocatedTime = time / mtg + inc * 3 / 4;
    allocatedTime = std::max<int64_t>(10, std::min<int64_t>(allocatedTime, time / 2));
  }

  Stack stack[MAX_PLY + 5] = {};
  Stack* ss = stack + 2;
  for (int i = 0; i < MAX_PLY; ++i) {
    (ss + i)->ply = i;
    (ss + i)->pv = pv_table[i];
    pv_table[i][0] = MOVE_NONE;
  }

  int maxDepth = limits.depth > 0 ? limits.depth : MAX_PLY - 2;
  Value alpha = -VALUE_INFINITE, beta = VALUE_INFINITE;
  Value bestScore = 0;
  Move lastBest = MOVE_NONE;

  for (int depth = 1; depth <= maxDepth; ++depth) {
    // Aspiration
    if (depth >= 5) {
      alpha = bestScore - 25;
      beta = bestScore + 25;
    } else {
      alpha = -VALUE_INFINITE;
      beta = VALUE_INFINITE;
    }

    while (true) {
      bestScore = search_node(pos, ss, alpha, beta, depth, false);
      if (info.stop) break;
      if (bestScore <= alpha) {
        alpha = -VALUE_INFINITE;
        continue;
      }
      if (bestScore >= beta) {
        beta = VALUE_INFINITE;
        continue;
      }
      break;
    }
    if (info.stop && depth > 1) break;

    if (ss->pv[0]) {
      lastBest = ss->pv[0];
      bestRootMove = lastBest;
    }

    // UCI info
    int64_t elapsed = std::max<int64_t>(1, now_ms() - startTime);
    std::cout << "info depth " << depth
              << " seldepth " << info.seldepth
              << " score cp " << bestScore
              << " nodes " << info.nodes
              << " nps " << (info.nodes * 1000 / elapsed)
              << " time " << elapsed
              << " pv";
    for (int i = 0; ss->pv[i]; ++i) std::cout << ' ' << move_to_uci(ss->pv[i]);
    std::cout << std::endl;

    if (limits.depth && depth >= limits.depth) break;
    if (allocatedTime > 0 && (now_ms() - startTime) > allocatedTime * 6 / 10) break;
  }

  if (!bestRootMove) {
    MoveListWrapper list(pos);
    if (list.size()) bestRootMove = list.begin()->move;
  }
  return bestRootMove;
}

} // namespace ah
