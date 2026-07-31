#include "search/search.hpp"
#include "eval/eval.hpp"
#include "movegen/movegen.hpp"
#include "search/book.hpp"
#include <chrono>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <cmath>

namespace ah {

namespace {
constexpr int FutilityMargin = 125;
constexpr int RazorMargin = 250;
constexpr int ReverseFutilityMargin = 95;

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

int piece_value(PieceType pt) {
  static constexpr int v[] = {0, 100, 320, 330, 500, 900, 0};
  return v[pt];
}
} // namespace

Search::Search() {
  tt.resize(256);
  clear();
}

void Search::clear() {
  tt.clear();
  std::memset(history, 0, sizeof(history));
  std::memset(captureHistory, 0, sizeof(captureHistory));
  std::memset(countermove, 0, sizeof(countermove));
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
  Move cm = MOVE_NONE;
  if (ss->ply > 0 && (ss - 1)->current) {
    Piece pc = pos.piece_on((ss - 1)->current.to());
    // After undo, piece is on from; use previous move destination piece via history key
    // Countermove indexed by the move that just led here: piece that moved onto to-square.
    // At ordering time that piece sits on (ss-1)->current.to().
    if (pc) cm = countermove[pc][(ss - 1)->current.to()];
  }

  for (ExtMove* m = begin; m != end; ++m) {
    Move mv = m->move;
    if (mv == ttMove) {
      m->score = 2'000'000;
    } else if (pos.piece_on(mv.to()) || mv.type() == EN_PASSANT) {
      int victim = mv.type() == EN_PASSANT ? PAWN : type_of(pos.piece_on(mv.to()));
      Piece attacker = pos.piece_on(mv.from());
      m->score = 1'000'000 + victim * 100 - type_of(attacker)
               + captureHistory[attacker][mv.to()][victim] / 8;
      if (mv.type() == PROMOTION) m->score += 800 + mv.promotion_type() * 20;
      if (!pos.see_ge(mv, -50)) m->score -= 450'000;
    } else if (mv.type() == PROMOTION) {
      m->score = 950'000 + mv.promotion_type() * 1000;
    } else if (mv == ss->killers[0]) {
      m->score = 900'000;
    } else if (mv == ss->killers[1]) {
      m->score = 800'000;
    } else if (mv == cm) {
      m->score = 750'000;
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
      int captureVal = m.type() == EN_PASSANT ? 100
                     : (m.type() == PROMOTION ? 900 : 0);
      if (m.type() != PROMOTION && pos.piece_on(m.to()))
        captureVal = piece_value(type_of(pos.piece_on(m.to())));
      if (stand + captureVal + 180 < alpha) continue;
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
  (ss + 1)->excluded = MOVE_NONE;

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

  // Mate distance pruning already applied; TT probe
  bool ttHit = false;
  TTEntry* tte = tt.probe(pos.key(), ttHit);
  Move ttMove = (!ss->excluded && ttHit) ? tte->move : MOVE_NONE;
  Value ttValue = ttHit ? value_from_tt(Value(tte->score), ss->ply) : VALUE_NONE;

  if (!pvNode && !ss->excluded && ttHit && int(tte->depth) >= depth && ttValue != VALUE_NONE) {
    if (tte->flag == TT_EXACT) return ttValue;
    if (tte->flag == TT_LOWER && ttValue >= beta) return ttValue;
    if (tte->flag == TT_UPPER && ttValue <= alpha) return ttValue;
  }

  const bool inCheck = pos.checkers();
  Value eval;
  if (inCheck) {
    eval = ss->staticEval = VALUE_NONE;
  } else if (ss->excluded) {
    eval = ss->staticEval;
  } else {
    eval = ss->staticEval = ttHit && tte->eval != int16_t(VALUE_NONE) ? Value(tte->eval) : evaluate(pos);
    // Correct eval with TT bound when useful
    if (ttHit && ttValue != VALUE_NONE) {
      if ((tte->flag == TT_LOWER && ttValue > eval) || (tte->flag == TT_UPPER && ttValue < eval) ||
          tte->flag == TT_EXACT)
        eval = ttValue;
    }
  }

  const bool improving = !inCheck && ss->ply >= 2 &&
      (ss - 2)->staticEval != VALUE_NONE && eval > (ss - 2)->staticEval;

  // Reverse futility pruning
  if (!rootNode && !pvNode && !inCheck && !ss->excluded && depth <= 8 &&
      eval - ReverseFutilityMargin * depth - (improving ? 0 : 40) >= beta)
    return eval;

  // Razoring
  if (!pvNode && !inCheck && !ss->excluded && depth <= 3 && eval + RazorMargin * depth <= alpha)
    return qsearch(pos, ss, alpha, beta);

  // Null move
  if (!pvNode && !inCheck && !ss->excluded && depth >= 2 && eval >= beta &&
      pos.non_pawn_material(pos.side_to_move()) &&
      (ss - 1)->current != MOVE_NULL &&
      ss->staticEval >= beta - 18 * depth + (improving ? 160 : 220)) {
    StateInfo st;
    int R = 3 + depth / 3 + std::min(3, (eval - beta) / 200);
    pos.do_null_move(st);
    Value nullScore = -search_node(pos, ss + 1, -beta, -beta + 1, depth - R, !cutNode);
    pos.undo_null_move();
    if (info.stop) return alpha;
    if (nullScore >= beta)
      return nullScore >= VALUE_MATE_IN_MAX_PLY ? beta : nullScore;
  }

  // ProbCut: try a shallow capture search with raised beta
  if (!pvNode && !inCheck && !ss->excluded && depth >= 5 && std::abs(beta) < VALUE_MATE_IN_MAX_PLY) {
    Value probBeta = beta + 160;
    ExtMove caps[MAX_MOVES];
    ExtMove* cend = generate<CAPTURES>(pos, caps);
    ExtMove* cn = caps;
    for (ExtMove* m = caps; m != cend; ++m)
      if (pos.is_legal(m->move)) *cn++ = *m;
    cend = cn;
    order_moves(pos, caps, cend, ttMove, ss);
    StateInfo st;
    int tried = 0;
    for (ExtMove* em = caps; em != cend && tried < 4; ++em) {
      Move m = em->move;
      if (!pos.see_ge(m, std::max(1, int(probBeta - eval)))) continue;
      ++tried;
      pos.do_move(m, st);
      Value score = -search_node(pos, ss + 1, -probBeta, -probBeta + 1, depth - 4, !cutNode);
      pos.undo_move(m);
      if (info.stop) return alpha;
      if (score >= probBeta) return score;
    }
  }

  // Internal iterative reduction when no TT move
  if (!ttMove && depth >= 5)
    depth -= 1 + (!pvNode);

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
  int quietFail = 0;

  for (ExtMove* em = moves; em != end; ++em) {
    Move m = em->move;
    if (m == ss->excluded) continue;

    if (rootNode && !limits.searchmoves.empty()) {
      bool found = false;
      for (Move sm : limits.searchmoves) if (sm == m) { found = true; break; }
      if (!found) continue;
    }

    ++moveCount;
    ss->current = m;
    ss->moveCount = moveCount;
    bool givesCheck = pos.gives_check(m);
    bool capture = pos.piece_on(m.to()) || m.type() == EN_PASSANT || m.type() == PROMOTION;

    // Futility
    if (!rootNode && !pvNode && !inCheck && !capture && !givesCheck && depth <= 7 &&
        eval + FutilityMargin * depth <= alpha && moveCount > 1)
      continue;

    // Late move pruning
    if (!rootNode && !pvNode && !capture && !givesCheck && depth <= 5 &&
        moveCount > (improving ? 3 : 2) + depth * depth)
      continue;

    // SEE pruning for bad captures / quiet moves
    if (!rootNode && !pvNode && depth <= 6 && moveCount > 1) {
      if (capture && !givesCheck && !pos.see_ge(m, -piece_value(PAWN) * depth))
        continue;
      if (!capture && !givesCheck && !pos.see_ge(m, -20 * depth * depth))
        continue;
    }

    Depth newDepth = depth - 1;
    int extension = 0;
    if (!rootNode && givesCheck && pos.see_ge(m, 0)) extension = 1;
    // Recapture extension
    if (!rootNode && ss->ply >= 1 && (ss - 1)->current &&
        m.to() == (ss - 1)->current.to() && capture)
      extension = std::max(extension, 1);

    // Singular extension (simplified)
    if (!rootNode && !ss->excluded && depth >= 7 && m == ttMove && ttHit &&
        tte->flag != TT_UPPER && int(tte->depth) >= depth - 3 &&
        std::abs(ttValue) < VALUE_MATE_IN_MAX_PLY) {
      Value singularBeta = ttValue - 2 * depth;
      ss->excluded = m;
      Value singular = search_node(pos, ss, singularBeta - 1, singularBeta, (depth - 1) / 2, cutNode);
      ss->excluded = MOVE_NONE;
      if (info.stop) return alpha;
      if (singular < singularBeta) extension = std::max(extension, 1);
      else if (singular >= beta) return singular;
    }

    Depth reduction = 0;
    if (depth >= 3 && moveCount > 1 + pvNode && !capture && !givesCheck) {
      reduction = Depth(0.70 + std::log(double(depth)) * std::log(double(moveCount)) / 2.10);
      if (cutNode) ++reduction;
      if (!improving) ++reduction;
      if (ss->killers[0] == m || ss->killers[1] == m) reduction = std::max(0, reduction - 1);
      int h = history[pos.side_to_move()][m.from()][m.to()];
      if (h > 5000) reduction = std::max(0, reduction - 1);
      if (h < -2000) ++reduction;
      if (ttMove && (pos.piece_on(ttMove.to()) || ttMove.type() == EN_PASSANT)) ++reduction;
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
            int bonus = depth * depth;
            int& h = history[pos.side_to_move()][m.from()][m.to()];
            h += bonus - h * bonus / 16384;
            if (ss->ply > 0 && (ss - 1)->current) {
              Piece onTo = pos.piece_on((ss - 1)->current.to());
              if (onTo) countermove[onTo][(ss - 1)->current.to()] = m;
            }
            // Penalize earlier quiet fails
            for (ExtMove* q = moves; q != em; ++q) {
              Move qm = q->move;
              if (pos.piece_on(qm.to()) || qm.type() == EN_PASSANT || qm.type() == PROMOTION)
                continue;
              int& qh = history[pos.side_to_move()][qm.from()][qm.to()];
              qh -= bonus / 2;
            }
          } else if (pos.piece_on(m.to()) || m.type() == EN_PASSANT) {
            Piece attacker = pos.piece_on(m.from());
            int victim = m.type() == EN_PASSANT ? PAWN : type_of(pos.piece_on(m.to()));
            // After undo, pieces are back
            attacker = pos.piece_on(m.from());
            int& ch = captureHistory[attacker][m.to()][victim];
            ch += depth * depth - ch * depth * depth / 16384;
          }
          break;
        }
      }
    } else if (!capture) {
      int& h = history[pos.side_to_move()][m.from()][m.to()];
      h -= depth * depth / 2;
      ++quietFail;
      (void)quietFail;
    }
  }

  if (!info.stop && !ss->excluded)
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

  // Opening book for short time controls / early plies
  if (!limits.infinite && pos.game_ply() <= 10) {
    Move bookMove = probe_book(pos);
    if (bookMove) {
      std::cout << "info string book " << move_to_uci(bookMove) << std::endl;
      return bookMove;
    }
  }

  startTime = now_ms();
  allocatedTime = 0;
  if (limits.movetime > 0) {
    // Use almost all of the allotted move time (leave a tiny buffer)
    allocatedTime = std::max(8, limits.movetime - 8);
  } else if (limits.wtime || limits.btime) {
    int time = pos.side_to_move() == WHITE ? limits.wtime : limits.btime;
    int inc = pos.side_to_move() == WHITE ? limits.winc : limits.binc;
    int mtg = limits.movestogo > 0 ? limits.movestogo : 30;
    allocatedTime = time / mtg + inc * 4 / 5;
    allocatedTime = std::max<int64_t>(20, std::min<int64_t>(allocatedTime, time * 4 / 5));
  }

  Stack stack[MAX_PLY + 5] = {};
  Stack* ss = stack + 2;
  for (int i = 0; i < MAX_PLY; ++i) {
    (ss + i)->ply = i;
    (ss + i)->pv = pv_table[i];
    pv_table[i][0] = MOVE_NONE;
  }
  // Sentinel for null-move / improving checks
  (ss - 1)->current = MOVE_NULL;
  (ss - 1)->staticEval = VALUE_NONE;
  (ss - 2)->staticEval = VALUE_NONE;

  int maxDepth = limits.depth > 0 ? limits.depth : MAX_PLY - 2;
  Value alpha = -VALUE_INFINITE, beta = VALUE_INFINITE;
  Value bestScore = 0;

  for (int depth = 1; depth <= maxDepth; ++depth) {
    int delta = 18 + depth;
    if (depth >= 4) {
      alpha = bestScore - delta;
      beta = bestScore + delta;
    } else {
      alpha = -VALUE_INFINITE;
      beta = VALUE_INFINITE;
    }

    while (true) {
      bestScore = search_node(pos, ss, alpha, beta, depth, false);
      if (info.stop) break;
      if (bestScore <= alpha) {
        beta = (alpha + beta) / 2;
        alpha = bestScore - delta;
        delta += delta / 2 + 4;
        continue;
      }
      if (bestScore >= beta) {
        beta = bestScore + delta;
        delta += delta / 2 + 4;
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
    if (allocatedTime > 0 && (now_ms() - startTime) > allocatedTime * 88 / 100) break;
    if (std::abs(bestScore) > VALUE_MATE_IN_MAX_PLY) break;
  }

  if (!bestRootMove) {
    MoveListWrapper list(pos);
    if (list.size()) bestRootMove = list.begin()->move;
  }
  return bestRootMove;
}

} // namespace ah
