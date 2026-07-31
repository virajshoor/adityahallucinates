#include "search/search.hpp"
#include "eval/eval.hpp"
#include "movegen/movegen.hpp"
#include <chrono>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <cmath>

namespace ah {

namespace {
constexpr int FutilityMargin = 120;
constexpr int RazorMargin = 250;
constexpr int ReverseFutilityMargin = 100;

Value value_to_tt(Value v, int ply) {
  if (v >= VALUE_MATE_IN_MAX_PLY) return v + ply;
  if (v <= -VALUE_MATE_IN_MAX_PLY) return v - ply;
  return v;
}
Value value_from_tt(Value v, int ply) {
  if (v >= VALUE_MATE_IN_MAX_PLY) return v - ply;
  if (v <= -VALUE_MATE_IN_MAX_PLY) return v + ply;
  return v;
}
} // namespace

Search::Search() {
  tt.resize(128);
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
  if (info.stop.load(std::memory_order_relaxed)) return true;
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
      m->score = 2'000'000;
    } else if (pos.piece_on(mv.to()) || mv.type() == EN_PASSANT) {
      int victim = mv.type() == EN_PASSANT ? PAWN : type_of(pos.piece_on(mv.to()));
      int attacker = type_of(pos.piece_on(mv.from()));
      m->score = 1'000'000 + victim * 100 - attacker;
      if (mv.type() == PROMOTION) m->score += 800 + mv.promotion_type() * 20;
      if (!pos.see_ge(mv, -50)) m->score -= 400'000;
    } else if (mv.type() == PROMOTION) {
      m->score = 950'000 + mv.promotion_type() * 1000;
    } else if (mv == ss->killers[0]) {
      m->score = 900'000;
    } else if (mv == ss->killers[1]) {
      m->score = 800'000;
    } else {
      m->score = history[pos.side_to_move()][mv.from()][mv.to()];
    }
  }
  std::stable_sort(begin, end);
}

Value Search::qsearch(Position& pos, Stack* ss, Value alpha, Value beta) {
  ++info.nodes;
  if ((info.nodes & 4095) == 0 && time_up()) {
    info.stop = true;
    return alpha;
  }

  ss->pv[0] = MOVE_NONE;
  if (ss->ply >= MAX_PLY - 1) return evaluate(pos);

  if (pos.is_draw(ss->ply)) return VALUE_DRAW;

  Value stand = evaluate(pos);
  if (stand >= beta) return stand;
  if (stand > alpha) alpha = stand;

  ExtMove moves[MAX_MOVES];
  ExtMove* end;
  if (pos.checkers()) {
    end = generate<LEGAL>(pos, moves);
    if (moves == end) return mated_in(ss->ply);
  } else {
    end = generate<CAPTURES>(pos, moves);
    ExtMove* n = moves;
    for (ExtMove* m = moves; m != end; ++m)
      if (pos.is_legal(m->move)) *n++ = *m;
    end = n;
  }

  order_moves(pos, moves, end, MOVE_NONE, ss);
  StateInfo st;

  for (ExtMove* em = moves; em != end; ++em) {
    Move m = em->move;
    if (!pos.checkers()) {
      // Delta pruning
      int captureVal = m.type() == EN_PASSANT ? 100
                     : (m.type() == PROMOTION ? 900 : 0);
      if (m.type() != PROMOTION && pos.piece_on(m.to()))
        captureVal = (type_of(pos.piece_on(m.to())) == PAWN ? 100 :
                      type_of(pos.piece_on(m.to())) == KNIGHT ? 320 :
                      type_of(pos.piece_on(m.to())) == BISHOP ? 330 :
                      type_of(pos.piece_on(m.to())) == ROOK ? 500 : 900);
      if (stand + captureVal + 150 < alpha) continue;
      if (!pos.see_ge(m, 0)) continue;
    }

    pos.do_move(m, st);
    Value score = -qsearch(pos, ss + 1, -beta, -alpha);
    pos.undo_move(m);
    if (info.stop) return alpha;

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

  if ((info.nodes & 4095) == 0 && time_up()) {
    info.stop = true;
    return alpha;
  }

  ss->pv[0] = MOVE_NONE;
  (ss + 1)->ply = ss->ply + 1;
  (ss + 1)->killers[0] = (ss + 1)->killers[1] = MOVE_NONE;

  if (!rootNode) {
    if (pos.is_draw(ss->ply) || ss->ply >= MAX_PLY - 1)
      return VALUE_DRAW;
  }

  alpha = std::max(alpha, mated_in(ss->ply));
  beta = std::min(beta, mate_in(ss->ply + 1));
  if (alpha >= beta) return alpha;

  if (depth <= 0)
    return qsearch(pos, ss, alpha, beta);

  info.seldepth = std::max(info.seldepth, ss->ply);

  bool ttHit = false;
  TTEntry* tte = tt.probe(pos.key(), ttHit);
  Move ttMove = ttHit ? tte->move : MOVE_NONE;
  Value ttValue = ttHit ? value_from_tt(Value(tte->score), ss->ply) : VALUE_NONE;

  if (!pvNode && ttHit && int(tte->depth) >= depth && ttValue != VALUE_NONE) {
    if (tte->flag == TT_EXACT) return ttValue;
    if (tte->flag == TT_LOWER && ttValue >= beta) return ttValue;
    if (tte->flag == TT_UPPER && ttValue <= alpha) return ttValue;
  }

  const bool inCheck = pos.checkers();
  Value eval;
  if (inCheck) {
    eval = ss->staticEval = VALUE_NONE;
  } else {
    eval = ss->staticEval = ttHit && tte->eval != int16_t(VALUE_NONE) ? Value(tte->eval) : evaluate(pos);
  }

  // Reverse futility pruning
  if (!pvNode && !inCheck && depth <= 6 && eval - ReverseFutilityMargin * depth >= beta)
    return eval;

  // Razoring
  if (!pvNode && !inCheck && depth <= 3 && eval + RazorMargin * depth < alpha)
    return qsearch(pos, ss, alpha, beta);

  // Null move
  if (!pvNode && !inCheck && depth >= 2 && eval >= beta &&
      pos.non_pawn_material(pos.side_to_move()) && ss->staticEval >= beta - 20 * depth + 200) {
    StateInfo st;
    int R = 3 + depth / 3;
    pos.do_null_move(st);
    Value nullScore = -search_node(pos, ss + 1, -beta, -beta + 1, depth - R, !cutNode);
    pos.undo_null_move();
    if (info.stop) return alpha;
    if (nullScore >= beta)
      return nullScore >= VALUE_MATE_IN_MAX_PLY ? beta : nullScore;
  }

  // Internal iterative reduction / generate TT move hint
  if (!ttMove && depth >= 4 && (pvNode || cutNode))
    depth -= 1;

  ExtMove moves[MAX_MOVES];
  ExtMove* end = generate<LEGAL>(pos, moves);
  if (moves == end)
    return inCheck ? mated_in(ss->ply) : VALUE_DRAW;

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
      for (Move sm : limits.searchmoves) if (sm == m) { found = true; break; }
      if (!found) continue;
    }

    ++moveCount;
    ss->current = m;
    bool givesCheck = pos.gives_check(m);
    bool capture = pos.piece_on(m.to()) || m.type() == EN_PASSANT || m.type() == PROMOTION;

    // Futility
    if (!rootNode && !pvNode && !inCheck && !capture && !givesCheck && depth <= 6 &&
        eval + FutilityMargin * depth <= alpha && moveCount > 1)
      continue;

    // Late move pruning
    if (!rootNode && !pvNode && !capture && !givesCheck && depth <= 5 &&
        moveCount > 3 + depth * depth)
      continue;

    Depth newDepth = depth - 1;
    int extension = 0;
    if (!rootNode && givesCheck) extension = 1;
    if (!rootNode && depth >= 6 && m == ttMove && ttHit && tte->depth >= depth - 3 &&
        tte->flag != TT_UPPER)
      extension = std::max(extension, 1); // crude singular-ish

    Depth reduction = 0;
    if (depth >= 3 && moveCount > 1 + pvNode && !capture && !givesCheck) {
      reduction = Depth(0.75 + std::log(depth) * std::log(moveCount) / 2.25);
      if (cutNode) ++reduction;
      if (ss->killers[0] == m || ss->killers[1] == m) reduction = std::max(0, reduction - 1);
      if (history[pos.side_to_move()][m.from()][m.to()] > 4000) reduction = std::max(0, reduction - 1);
      reduction = std::clamp(reduction, 0, newDepth - 1 + extension);
    }

    pos.do_move(m, st);
    Value score;
    if (moveCount == 1) {
      score = -search_node(pos, ss + 1, -beta, -alpha, newDepth + extension, false);
    } else {
      score = -search_node(pos, ss + 1, -alpha - 1, -alpha, newDepth + extension - reduction, true);
      if (reduction && score > alpha)
        score = -search_node(pos, ss + 1, -alpha - 1, -alpha, newDepth + extension, !cutNode);
      if (pvNode && score > alpha && (rootNode || score < beta))
        score = -search_node(pos, ss + 1, -beta, -alpha, newDepth + extension, false);
    }
    pos.undo_move(m);
    if (info.stop) return alpha;

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
            int& h = history[pos.side_to_move()][m.from()][m.to()];
            h += depth * depth - h * depth * depth / 16384;
          }
          break;
        }
      }
    } else if (!capture) {
      int& h = history[pos.side_to_move()][m.from()][m.to()];
      h -= depth * depth / 2;
    }
  }

  if (!info.stop)
    tt.store(pos.key(), depth, value_to_tt(bestScore, ss->ply), flag,
             bestMove ? bestMove : ttMove, ss->staticEval);
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
    allocatedTime = std::max(5, limits.movetime - 15);
  } else if (limits.wtime || limits.btime) {
    int time = pos.side_to_move() == WHITE ? limits.wtime : limits.btime;
    int inc = pos.side_to_move() == WHITE ? limits.winc : limits.binc;
    int mtg = limits.movestogo > 0 ? limits.movestogo : 28;
    allocatedTime = time / mtg + inc * 3 / 4;
    allocatedTime = std::max<int64_t>(15, std::min<int64_t>(allocatedTime, time * 4 / 5));
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

  for (int depth = 1; depth <= maxDepth; ++depth) {
    if (depth >= 5) {
      alpha = bestScore - 20;
      beta = bestScore + 20;
    } else {
      alpha = -VALUE_INFINITE;
      beta = VALUE_INFINITE;
    }

    int delta = 20;
    while (true) {
      bestScore = search_node(pos, ss, alpha, beta, depth, false);
      if (info.stop) break;
      if (bestScore <= alpha) {
        beta = (alpha + beta) / 2;
        alpha = bestScore - delta;
        delta += delta / 2;
        continue;
      }
      if (bestScore >= beta) {
        beta = bestScore + delta;
        delta += delta / 2;
        continue;
      }
      break;
    }
    if (info.stop && depth > 1) break;

    if (ss->pv[0]) bestRootMove = ss->pv[0];

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
    if (allocatedTime > 0 && (now_ms() - startTime) > allocatedTime * 65 / 100) break;
    // Mate found
    if (std::abs(bestScore) > VALUE_MATE_IN_MAX_PLY) break;
  }

  if (!bestRootMove) {
    MoveListWrapper list(pos);
    if (list.size()) bestRootMove = list.begin()->move;
  }
  return bestRootMove;
}

} // namespace ah
