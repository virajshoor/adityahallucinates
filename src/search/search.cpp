#include "search/search.hpp"
#include "eval/eval.hpp"
#include "movegen/movegen.hpp"
#include "search/book.hpp"
#include "nnue/nnue.hpp"
#include <chrono>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <thread>

namespace ah {

namespace {
constexpr int FutilityMargin = 150;
constexpr int RazorMargin = 300;
constexpr int ReverseFutilityMargin = 145;

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

void atomic_max(std::atomic<int>& a, int v) {
  int cur = a.load(std::memory_order_relaxed);
  while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}
} // namespace


Search::Search() : tt(std::make_shared<TranspositionTable>()), info(&ownedInfo) {
  tt->resize(256);
  clear();
}

Search::Search(std::shared_ptr<TranspositionTable> sharedTt, SearchInfo* sharedInfo)
    : tt(std::move(sharedTt)), info(sharedInfo) {
  std::memset(history, 0, sizeof(history));
  std::memset(captureHistory, 0, sizeof(captureHistory));
  std::memset(contHistory, 0, sizeof(contHistory));
  std::memset(countermove, 0, sizeof(countermove));
  std::memset(corrHist, 0, sizeof(corrHist));
  std::memset(pv_table, 0, sizeof(pv_table));
  silent = true;
}

void Search::set_threads(int n) {
  numThreads = std::clamp(n, 1, 8);
}

void Search::clear() {
  tt->clear();
  std::memset(history, 0, sizeof(history));
  std::memset(captureHistory, 0, sizeof(captureHistory));
  std::memset(contHistory, 0, sizeof(contHistory));
  std::memset(countermove, 0, sizeof(countermove));
  std::memset(corrHist, 0, sizeof(corrHist));
  std::memset(pv_table, 0, sizeof(pv_table));
  info->nodes.store(0, std::memory_order_relaxed);
  info->seldepth.store(0, std::memory_order_relaxed);
  info->stop.store(false, std::memory_order_relaxed);
}

void Search::add_history(int& h, int bonus) {
  h += bonus - h * std::abs(bonus) / 16384;
  h = std::clamp(h, -16384, 16384);
}

void Search::update_quiet_stats(Position& pos, Stack* ss, Move best,
                                const Move* quiets, int quietCount, Depth depth) {
  const int bonus = depth * depth;
  const Color us = pos.side_to_move();
  add_history(history[us][best.from()][best.to()], bonus);

  if (ss->killers[0] != best) {
    ss->killers[1] = ss->killers[0];
    ss->killers[0] = best;
  }

  auto apply_cont = [&](int plyBack, int delta) {
    if (ss->ply < plyBack) return;
    Stack* s = ss - plyBack;
    if (!s->movedPiece || !s->current) return;
    add_history(contHistory[plyBack - 1][s->movedPiece][s->current.to()][best.to()], delta);
    if (plyBack == 1) countermove[s->movedPiece][s->current.to()] = best;
  };
  apply_cont(1, bonus);
  apply_cont(2, bonus / 2);

  for (int i = 0; i < quietCount; ++i) {
    Move q = quiets[i];
    if (q == best) continue;
    add_history(history[us][q.from()][q.to()], -bonus);
    if (ss->ply >= 1 && (ss - 1)->movedPiece)
      add_history(contHistory[0][(ss - 1)->movedPiece][(ss - 1)->current.to()][q.to()], -bonus);
    if (ss->ply >= 2 && (ss - 2)->movedPiece)
      add_history(contHistory[1][(ss - 2)->movedPiece][(ss - 2)->current.to()][q.to()], -bonus / 2);
  }
}

void Search::update_capture_stats(Position& pos, Move best, const Move* caps,
                                  int capCount, Depth depth) {
  const int bonus = depth * depth;
  auto hist_for = [&](Move m) -> int& {
    Piece attacker = pos.piece_on(m.from());
    int victim = m.type() == EN_PASSANT ? PAWN
               : (pos.piece_on(m.to()) ? type_of(pos.piece_on(m.to())) : PAWN);
    return captureHistory[attacker][m.to()][victim];
  };
  // Called before undo — board still has the capture position.
  add_history(hist_for(best), bonus);
  for (int i = 0; i < capCount; ++i) {
    if (caps[i] == best) continue;
    add_history(hist_for(caps[i]), -bonus);
  }
}

int64_t Search::now_ms() const {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool Search::time_up() const {
  if (info->stop.load(std::memory_order_relaxed)) return true;
  if (limits.infinite) return false;
  const int64_t elapsed = now_ms() - startTime;
  if (hardDeadline > 0 && now_ms() >= hardDeadline) return true;
  if (limits.movetime > 0 && elapsed >= limits.movetime) return true;
  if (limits.depth && !limits.movetime && !limits.wtime && !limits.btime) return false;
  if (allocatedTime <= 0) return false;
  return elapsed >= allocatedTime;
}

Value Search::eval_pos(const Position& pos, Stack* ss) const {
  if (useNnueAcc) return evaluate(pos, &ss->acc);
  return evaluate(pos);
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
  Piece prevPc = NO_PIECE, prev2Pc = NO_PIECE;
  Square prevTo = SQ_NONE, prev2To = SQ_NONE;
  if (ss->ply > 0 && (ss - 1)->current && (ss - 1)->movedPiece) {
    prevPc = (ss - 1)->movedPiece;
    prevTo = (ss - 1)->current.to();
    cm = countermove[prevPc][prevTo];
  }
  if (ss->ply > 1 && (ss - 2)->current && (ss - 2)->movedPiece) {
    prev2Pc = (ss - 2)->movedPiece;
    prev2To = (ss - 2)->current.to();
  }

  for (ExtMove* m = begin; m != end; ++m) {
    Move mv = m->move;
    if (mv == ttMove) {
      m->score = 2'000'000;
    } else if (pos.piece_on(mv.to()) || mv.type() == EN_PASSANT) {
      int victim = mv.type() == EN_PASSANT ? PAWN : type_of(pos.piece_on(mv.to()));
      Piece attacker = pos.piece_on(mv.from());
      m->score = 1'000'000 + victim * 100 - type_of(attacker)
               + captureHistory[attacker][mv.to()][victim] / 16;
      if (mv.type() == PROMOTION) m->score += 800 + mv.promotion_type() * 20;
      if (!pos.see_ge(mv, -50)) m->score -= 400'000;
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
      if (prevPc) m->score += contHistory[0][prevPc][prevTo][mv.to()] / 4;
      if (prev2Pc) m->score += contHistory[1][prev2Pc][prev2To][mv.to()] / 8;
    }
  }
  std::stable_sort(begin, end);
}

Value Search::qsearch(Position& pos, Stack* ss, Value alpha, Value beta) {
  info->nodes.fetch_add(1, std::memory_order_relaxed);
  // Poll the hard deadline frequently; don't wait for a 1024-node boundary.
  if (hardDeadline > 0 && (info->nodes.load(std::memory_order_relaxed) & 63) == 0 && now_ms() >= hardDeadline) {
    info->stop.store(true, std::memory_order_relaxed);
    return alpha;
  }
  if ((info->nodes.load(std::memory_order_relaxed) & 1023) == 0 && time_up()) {
    info->stop.store(true, std::memory_order_relaxed);
    return alpha;
  }

  ss->pv[0] = MOVE_NONE;
  if (ss->ply >= MAX_PLY - 1) return eval_pos(pos, ss);

  if (pos.is_draw(ss->ply)) {
    Value stand = eval_pos(pos, ss);
    if (std::abs(int(stand)) > 80) return Value(stand / 5);
    return VALUE_DRAW;
  }

  // Quiescence TT — major NPS/quality win at fixed movetime.
  bool ttHit = false;
  TTEntry* tte = tt->probe(pos.key(), ttHit);
  Move ttMove = ttHit ? tte->move : MOVE_NONE;
  Value ttValue = ttHit ? value_from_tt(Value(tte->score), ss->ply) : VALUE_NONE;
  if (ttHit && ttValue != VALUE_NONE) {
    if (tte->flag == TT_EXACT) return ttValue;
    if (tte->flag == TT_LOWER && ttValue >= beta) return ttValue;
    if (tte->flag == TT_UPPER && ttValue <= alpha) return ttValue;
  }

  const bool inCheck = pos.checkers();
  Value stand;
  if (inCheck) {
    stand = -VALUE_INFINITE;
  } else {
    stand = ttHit && tte->eval != int16_t(VALUE_NONE) ? Value(tte->eval) : eval_pos(pos, ss);
    if (stand >= beta) {
      if (!ttHit)
        tt->store(pos.key(), 0, value_to_tt(stand, ss->ply), TT_LOWER, MOVE_NONE, stand);
      return stand;
    }
    if (stand > alpha) alpha = stand;
  }

  ExtMove moves[MAX_MOVES];
  ExtMove* end;
  if (inCheck) {
    end = generate<LEGAL>(pos, moves);
    if (moves == end) return mated_in(ss->ply);
  } else {
    end = generate<CAPTURES>(pos, moves);
    ExtMove* n = moves;
    for (ExtMove* m = moves; m != end; ++m)
      if (pos.is_legal(m->move)) *n++ = *m;
    // NOTE: quiet checks in qsearch previously caused multi-hour hangs mid-match
    // (explosion past clock polls). Keep qsearch to captures only.
    end = n;
  }

  order_moves(pos, moves, end, ttMove, ss);
  StateInfo st;
  Move bestMove = MOVE_NONE;
  Value bestScore = stand;
  TTFlag flag = TT_UPPER;

  for (ExtMove* em = moves; em != end; ++em) {
    Move m = em->move;
    if (!inCheck) {
      int captureVal = m.type() == EN_PASSANT ? 100
                     : (m.type() == PROMOTION ? 900 : 0);
      if (m.type() != PROMOTION && pos.piece_on(m.to()))
        captureVal = piece_value(type_of(pos.piece_on(m.to())));
      if (stand + captureVal + 150 < alpha) continue;
      if (!pos.see_ge(m, 0)) continue;
    }

    if (useNnueAcc) nnue().do_move((ss + 1)->acc, ss->acc, pos, m);
    pos.do_move(m, st);
    Value score = -qsearch(pos, ss + 1, -beta, -alpha);
    pos.undo_move(m);
    if (info->stop.load(std::memory_order_relaxed)) return alpha;

    if (score > bestScore) {
      bestScore = score;
      if (score > alpha) {
        alpha = score;
        bestMove = m;
        flag = TT_EXACT;
        update_pv(ss, m);
        if (score >= beta) {
          flag = TT_LOWER;
          break;
        }
      }
    }
  }

  if (!info->stop.load(std::memory_order_relaxed))
    tt->store(pos.key(), 0, value_to_tt(bestScore, ss->ply), flag,
              bestMove ? bestMove : ttMove, inCheck ? VALUE_NONE : stand);
  return bestScore;
}

Value Search::search_node(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode) {
  const bool rootNode = (ss->ply == 0);
  const bool pvNode = (beta - alpha) > 1;
  info->nodes.fetch_add(1, std::memory_order_relaxed);

  if (hardDeadline > 0 && (info->nodes.load(std::memory_order_relaxed) & 63) == 0 && now_ms() >= hardDeadline) {
    info->stop.store(true, std::memory_order_relaxed);
    return alpha;
  }
  if ((info->nodes.load(std::memory_order_relaxed) & 1023) == 0 && time_up()) {
    info->stop.store(true, std::memory_order_relaxed);
    return alpha;
  }

  ss->pv[0] = MOVE_NONE;
  (ss + 1)->ply = ss->ply + 1;
  (ss + 1)->killers[0] = (ss + 1)->killers[1] = MOVE_NONE;
  (ss + 1)->excludedMove = MOVE_NONE;

  if (!rootNode) {
    if (ss->ply >= MAX_PLY - 1) return VALUE_DRAW;
  }

  alpha = std::max(alpha, mated_in(ss->ply));
  beta = std::min(beta, mate_in(ss->ply + 1));
  if (alpha >= beta) return alpha;

  if (depth <= 0)
    return qsearch(pos, ss, alpha, beta);

  atomic_max(info->seldepth, ss->ply);

  const Move excludedMove = ss->excludedMove;
  const bool singularSearch = excludedMove != MOVE_NONE;

  bool ttHit = false;
  TTEntry* tte = tt->probe(pos.key(), ttHit);
  Move ttMove = ttHit ? tte->move : MOVE_NONE;
  Value ttValue = ttHit ? value_from_tt(Value(tte->score), ss->ply) : VALUE_NONE;

  // Skip TT cutoffs during singular verification (hash is for the full position).
  if (!pvNode && !singularSearch && ttHit && int(tte->depth) >= depth && ttValue != VALUE_NONE) {
    if (tte->flag == TT_EXACT) return ttValue;
    if (tte->flag == TT_LOWER && ttValue >= beta) return ttValue;
    if (tte->flag == TT_UPPER && ttValue <= alpha) return ttValue;
  }

  const bool inCheck = pos.checkers();
  Value eval;
  Value rawEval = VALUE_NONE;
  if (inCheck) {
    eval = ss->staticEval = VALUE_NONE;
  } else {
    rawEval = ttHit && tte->eval != int16_t(VALUE_NONE) ? Value(tte->eval) : eval_pos(pos, ss);
    ss->staticEval = rawEval;
    // Correction history (Stockfish-style): nudge static eval from prior residuals.
    const int corr = corrHist[pos.side_to_move()][pos.key() & (CORR_SIZE - 1)];
    eval = Value(std::clamp(int(rawEval) + corr / 32, -VALUE_INFINITE + 1, VALUE_INFINITE - 1));
  }

  // 2-fold / rule50 draw: soft-draw toward eval when clearly better/worse.
  // Prefer continuing when clearly winning (conversion); /5 was too drawish vs Elo3000.
  if (!rootNode && pos.is_draw(ss->ply)) {
    if (!inCheck) {
      const int ae = std::abs(int(eval));
      if (ae > 250) return Value(eval / 2);
      if (ae > 100) return Value(eval * 2 / 5);
      if (ae > 80) return Value(eval / 5);
    }
    return VALUE_DRAW;
  }

  const bool improving = !inCheck && ss->ply >= 2 &&
      (ss - 2)->staticEval != VALUE_NONE && rawEval > (ss - 2)->staticEval;

  // Reverse futility pruning
  if (!pvNode && !inCheck && depth <= 7 &&
      eval - ReverseFutilityMargin * depth - (improving ? 0 : 35) >= beta)
    return eval;

  // Razoring
  if (!pvNode && !inCheck && depth <= 3 && eval + RazorMargin * depth < alpha)
    return qsearch(pos, ss, alpha, beta);

  // Null move with verification at higher depths (avoids zugzwang cutoffs)
  if (!pvNode && !singularSearch && !inCheck && depth >= 2 && eval >= beta &&
      pos.non_pawn_material(pos.side_to_move()) &&
      (ss - 1)->current != MOVE_NULL &&
      eval >= beta - 20 * depth + (improving ? 180 : 220)) {
    StateInfo st;
    int R = 3 + depth / 3 + std::min(2, (eval - beta) / 220);
    if (useNnueAcc) (ss + 1)->acc.copy_from(ss->acc);
    Move prevMove = ss->current;
    ss->current = MOVE_NULL;
    pos.do_null_move(st);
    Value nullScore = -search_node(pos, ss + 1, -beta, -beta + 1, depth - R, !cutNode);
    pos.undo_null_move();
    ss->current = prevMove;
    if (info->stop.load(std::memory_order_relaxed)) return alpha;
    if (nullScore >= beta) {
      if (nullScore >= VALUE_MATE_IN_MAX_PLY) return beta;
      if (depth >= 10) {
        Value verify = search_node(pos, ss, beta - 1, beta, depth - R, false);
        if (info->stop.load(std::memory_order_relaxed)) return alpha;
        if (verify < beta) goto skip_null; // null move failed verification
      }
      return nullScore;
    }
  }
  skip_null:

  // ProbCut disabled: earlier aggressive variants regressed Elo 2000; revisit with SPRT.

  // Internal iterative deepening: shallow search to get a TT move (PV only)
  if (!singularSearch && pvNode && !ttMove && depth >= 6) {
    search_node(pos, ss, alpha, beta, depth - 2, false);
    if (info->stop.load(std::memory_order_relaxed)) return alpha;
    tte = tt->probe(pos.key(), ttHit);
    ttMove = ttHit ? tte->move : MOVE_NONE;
    ttValue = ttHit ? value_from_tt(Value(tte->score), ss->ply) : VALUE_NONE;
  } else if (!singularSearch && !ttMove && depth >= 7) {
    depth -= 1; // IIR on non-PV
  }

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
  Move quietsTried[64];
  Move capsTried[32];
  int quietCount = 0, capCount = 0;

  for (ExtMove* em = moves; em != end; ++em) {
    Move m = em->move;
    if (m == excludedMove) continue;
    if (rootNode && !limits.searchmoves.empty()) {
      bool found = false;
      for (Move sm : limits.searchmoves) if (sm == m) { found = true; break; }
      if (!found) continue;
    }

    ++moveCount;
    ss->current = m;
    ss->movedPiece = pos.piece_on(m.from());
    if (m.type() == PROMOTION) ss->movedPiece = make_piece(pos.side_to_move(), m.promotion_type());
    bool givesCheck = pos.gives_check(m);
    bool capture = pos.piece_on(m.to()) || m.type() == EN_PASSANT || m.type() == PROMOTION;

    // Futility
    if (!rootNode && !pvNode && !inCheck && !capture && !givesCheck && depth <= 6 &&
        eval + FutilityMargin * depth <= alpha && moveCount > 1)
      continue;

    // Late move pruning (less aggressive — accuracy over NPS at long TC)
    if (!rootNode && !pvNode && !capture && !givesCheck && depth <= 4 &&
        moveCount > (improving ? 5 : 4) + depth * depth + depth)
      continue;

    // Bad-capture SEE pruning
    if (!rootNode && !pvNode && capture && !givesCheck && depth <= 5 && moveCount > 1 &&
        !pos.see_ge(m, -piece_value(PAWN) * depth))
      continue;

    // Quiet moves that hang material (SEE < 0)
    if (!rootNode && !pvNode && !capture && !givesCheck && depth <= 6 && moveCount > 1 &&
        !pos.see_ge(m, 0))
      continue;

    Depth newDepth = depth - 1;
    int extension = 0;
    if (!rootNode && givesCheck && pos.see_ge(m, 0))
      extension = 1;
    if (!rootNode && ss->ply >= 1 && (ss - 1)->current &&
        m.to() == (ss - 1)->current.to() && capture)
      extension = std::max(extension, 1);
    // Proper singular extension: verify TT move is uniquely good via excluded search.
    // Conservative margins — aggressive singular previously regressed Elo 2000.
    if (!rootNode && !singularSearch && !extension && depth >= 8 && m == ttMove && ttHit &&
        tte->depth >= depth - 3 &&
        (tte->flag == TT_LOWER || tte->flag == TT_EXACT) &&
        std::abs(int(ttValue)) < VALUE_MATE_IN_MAX_PLY - 100) {
      Value singularBeta = Value(int(ttValue) - (2 + depth / 2));
      Depth singularDepth = depth / 2;
      if (singularDepth >= 1 && singularBeta > -VALUE_MATE_IN_MAX_PLY) {
        ss->excludedMove = m;
        Value singularValue = search_node(pos, ss, singularBeta - 1, singularBeta,
                                          singularDepth, cutNode);
        ss->excludedMove = MOVE_NONE;
        if (info->stop.load(std::memory_order_relaxed)) return alpha;
        if (singularValue < singularBeta)
          extension = 1;
      }
    }

    Depth reduction = 0;
    if (depth >= 3 && moveCount > 1 + pvNode && !givesCheck) {
      if (!capture) {
        reduction = Depth(0.65 + std::log(double(depth)) * std::log(double(moveCount)) / 2.50);
        if (cutNode) ++reduction;
        if (!improving) ++reduction;
        if (ss->killers[0] == m || ss->killers[1] == m) reduction = std::max(0, reduction - 1);
        int h = history[pos.side_to_move()][m.from()][m.to()];
        if (ss->ply >= 1 && (ss - 1)->movedPiece)
          h += contHistory[0][(ss - 1)->movedPiece][(ss - 1)->current.to()][m.to()] / 4;
        if (h > 4000) reduction = std::max(0, reduction - 1);
        if (h < -2000) ++reduction;
      } else if (moveCount > 3 && depth >= 4 && !pos.see_ge(m, -piece_value(PAWN))) {
        // Capture LMR only for late, SEE-negative-ish captures (not quiet LMR soften)
        reduction = Depth(1 + (moveCount > 6));
      }
      reduction = std::clamp(reduction, 0, newDepth - 1 + extension);
    }

    if (!capture && quietCount < 64) quietsTried[quietCount++] = m;
    if (capture && !givesCheck && m.type() != PROMOTION && capCount < 32) capsTried[capCount++] = m;

    if (useNnueAcc) nnue().do_move((ss + 1)->acc, ss->acc, pos, m);
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
    if (info->stop.load(std::memory_order_relaxed)) return alpha;

    if (score > bestScore) {
      bestScore = score;
      if (score > alpha) {
        bestMove = m;
        alpha = score;
        flag = TT_EXACT;
        update_pv(ss, m);
        if (score >= beta) {
          flag = TT_LOWER;
          // History gravity: bonus best, malus earlier tried moves (pre-undo board state
          // is restored — update_* use from/to + piece_on for captures before undo... 
          // We already undid; re-do capture hist using saved capture flag.
          if (!capture)
            update_quiet_stats(pos, ss, m, quietsTried, quietCount, depth);
          else if (capture && m.type() != PROMOTION) {
            // After undo, board is correct for captureHistory indexing.
            update_capture_stats(pos, m, capsTried, capCount, depth);
          }
          break;
        }
      }
    }
  }

  // Only legal move was excluded (singular verification of a unique move).
  if (moveCount == 0)
    return alpha;

  if (!info->stop.load(std::memory_order_relaxed) && !singularSearch) {
    tt->store(pos.key(), depth, value_to_tt(bestScore, ss->ply), flag,
             bestMove ? bestMove : ttMove, ss->staticEval);
    // Update correction history from search residual (non-mate, sufficient depth).
    if (!inCheck && !rootNode && depth >= 4 && rawEval != VALUE_NONE &&
        std::abs(int(bestScore)) < VALUE_MATE_IN_MAX_PLY - 100) {
      int diff = int(bestScore) - int(rawEval);
      diff = std::clamp(diff, -400, 400);
      int& ch = corrHist[pos.side_to_move()][pos.key() & (CORR_SIZE - 1)];
      // Stronger weight at deeper searches; keep bounded.
      int bonus = diff * depth / 8;
      ch += bonus - ch * std::abs(bonus) / 1024;
      ch = std::clamp(ch, -4096, 4096);
    }
  }
  return bestScore;
}

Move Search::think(Position& pos, const SearchLimits& lim) {
  limits = lim;
  info->stop.store(false, std::memory_order_relaxed);
  info->nodes.store(0, std::memory_order_relaxed);
  info->seldepth.store(0, std::memory_order_relaxed);
  tt->new_search();
  bestRootMove = MOVE_NONE;
  // Only maintain accumulators when the net actually contributes to eval.
  useNnueAcc = false;
  if (nnue_ready() && std::getenv("ADITYA_USE_NNUE") &&
      std::getenv("ADITYA_USE_NNUE")[0] == '1') {
    const char* b = std::getenv("ADITYA_NNUE_BLEND");
    const int blend = b ? std::clamp(std::atoi(b), 0, 100) : 70;
    useNnueAcc = blend < 100;
    // Escape hatch: full refresh every eval (debug / validate incremental).
    if (const char* r = std::getenv("ADITYA_NNUE_REFRESH"); r && r[0] == '1')
      useNnueAcc = false;
  }

  if (!limits.infinite && pos.game_ply() <= 14) {
    Move bookMove = probe_book(pos);
    if (bookMove) {
      std::cout << "info string book " << move_to_uci(bookMove) << std::endl;
      return bookMove;
    }
  }

  startTime = now_ms();
  allocatedTime = 0;
  hardDeadline = 0;
  if (limits.movetime > 0) {
    allocatedTime = std::max(8, limits.movetime - 10);
    hardDeadline = startTime + limits.movetime + 250;
  } else if (limits.wtime || limits.btime) {
    int time = pos.side_to_move() == WHITE ? limits.wtime : limits.btime;
    int inc = pos.side_to_move() == WHITE ? limits.winc : limits.binc;
    int mtg = limits.movestogo > 0 ? limits.movestogo : 28;
    allocatedTime = time / mtg + inc * 3 / 4;
    allocatedTime = std::max<int64_t>(15, std::min<int64_t>(allocatedTime, time * 4 / 5));
    hardDeadline = startTime + allocatedTime + 500;
  }
  if (!silent) {
    std::cout << "info string time_ctrl movetime=" << limits.movetime
              << " allocated=" << allocatedTime
              << " hard_ms=" << (hardDeadline ? (hardDeadline - startTime) : 0)
              << " threads=" << numThreads
              << std::endl;
  }

  // Lazy SMP: helpers run independent ID on a FEN copy, sharing TT + stop/nodes.
  std::vector<std::thread> helpers;
  const std::string rootFen = pos.fen();
  if (numThreads > 1) {
    helpers.reserve(numThreads - 1);
    for (int id = 1; id < numThreads; ++id)
      helpers.emplace_back([this, rootFen, id]() { helper_loop(rootFen, id); });
  }

  iterative_deepening(pos, /*emitInfo=*/!silent);

  // Stop helpers and join — main thread owns the returned best move.
  info->stop.store(true, std::memory_order_relaxed);
  for (auto& t : helpers) t.join();

  if (!bestRootMove) {
    MoveListWrapper list(pos);
    if (list.size()) bestRootMove = list.begin()->move;
  }
  return bestRootMove;
}

void Search::helper_loop(const std::string& fen, int helperId) {
  Search helper(tt, info);
  helper.limits = limits;
  helper.startTime = startTime;
  helper.allocatedTime = allocatedTime;
  helper.hardDeadline = hardDeadline;
  helper.useNnueAcc = useNnueAcc;
  helper.silent = true;
  helper.numThreads = 1;

  // Asymmetric history so helpers diverge in move ordering (Lazy SMP diversity).
  unsigned seed = 0x9e3779b9u * static_cast<unsigned>(helperId + 1);
  for (int c = 0; c < COLOR_NB; ++c)
    for (int f = 0; f < 64; ++f)
      for (int t = 0; t < 64; ++t) {
        seed = seed * 1664525u + 1013904223u;
        helper.history[c][f][t] = static_cast<int>(seed % 17) - 8;
      }

  Position hpos;
  StateInfo states[MAX_PLY + 8];
  hpos.set(fen, states[0]);
  // Offset start depth + aspiration width so TT fills different shapes.
  const int startDepth = 1 + (helperId % 3);
  const int aspBase = 24 + 4 * helperId;
  helper.iterative_deepening(hpos, /*emitInfo=*/false, startDepth, aspBase);
}

void Search::iterative_deepening(Position& pos, bool emitInfo, int startDepth, int aspBase) {
  Stack stack[MAX_PLY + 5] = {};
  Stack* ss = stack + 2;
  for (int i = 0; i < MAX_PLY; ++i) {
    (ss + i)->ply = i;
    (ss + i)->pv = pv_table[i];
    pv_table[i][0] = MOVE_NONE;
  }
  (ss - 1)->current = MOVE_NULL;
  (ss - 1)->staticEval = VALUE_NONE;
  (ss - 2)->staticEval = VALUE_NONE;
  if (useNnueAcc) nnue().refresh(ss->acc, pos);

  int maxDepth = limits.depth > 0 ? limits.depth : MAX_PLY - 2;
  if (limits.movetime > 0 || limits.wtime || limits.btime)
    maxDepth = std::min(maxDepth, 48);
  Value alpha = -VALUE_INFINITE, beta = VALUE_INFINITE;
  Value bestScore = 0;
  startDepth = std::max(1, startDepth);
  aspBase = std::max(12, aspBase);

  for (int depth = startDepth; depth <= maxDepth; ++depth) {
    if (time_up() || (hardDeadline > 0 && now_ms() >= hardDeadline)) {
      info->stop.store(true, std::memory_order_relaxed);
      break;
    }
    if (depth >= 5) {
      alpha = bestScore - aspBase;
      beta = bestScore + aspBase;
    } else {
      alpha = -VALUE_INFINITE;
      beta = VALUE_INFINITE;
    }

    int delta = aspBase;
    int aspTries = 0;
    while (true) {
      bestScore = search_node(pos, ss, alpha, beta, depth, false);
      if (info->stop.load(std::memory_order_relaxed) || time_up()) {
        info->stop.store(true, std::memory_order_relaxed);
        break;
      }
      if (bestScore <= alpha) {
        beta = (alpha + beta) / 2;
        alpha = bestScore - delta;
        delta += delta / 2;
        if (allocatedTime > 0) allocatedTime = std::min(allocatedTime + allocatedTime / 8,
            limits.movetime > 0 ? std::max<int64_t>(8, limits.movetime - 5)
                                : allocatedTime * 2);
        if (++aspTries >= 8) break;
        continue;
      }
      if (bestScore >= beta) {
        beta = bestScore + delta;
        delta += delta / 2;
        if (++aspTries >= 8) break;
        continue;
      }
      break;
    }
    if (info->stop.load(std::memory_order_relaxed)) break;

    if (ss->pv[0]) bestRootMove = ss->pv[0];
    if (emitInfo) {
      int64_t elapsed = std::max<int64_t>(1, now_ms() - startTime);
      const uint64_t nodes = info->nodes.load(std::memory_order_relaxed);
      std::cout << "info depth " << depth
                << " seldepth " << info->seldepth.load(std::memory_order_relaxed)
                << " score cp " << bestScore
                << " nodes " << nodes
                << " nps " << (nodes * 1000 / elapsed)
                << " time " << elapsed
                << " pv";
      for (int i = 0; ss->pv[i]; ++i) std::cout << ' ' << move_to_uci(ss->pv[i]);
      std::cout << std::endl;
    }

    if (limits.depth && depth >= limits.depth) break;
    if (allocatedTime > 0 && (now_ms() - startTime) > allocatedTime * 95 / 100) break;
    if (std::abs(bestScore) > VALUE_MATE_IN_MAX_PLY) break;
  }
}

} // namespace ah
