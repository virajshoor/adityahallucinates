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
} // namespace

Search::Search() {
  tt.resize(256);
  clear();
}

void Search::clear() {
  tt.clear();
  std::memset(history, 0, sizeof(history));
  std::memset(captureHistory, 0, sizeof(captureHistory));
  std::memset(contHistory, 0, sizeof(contHistory));
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
  Piece prevPc = NO_PIECE;
  Square prevTo = SQ_NONE;
  if (ss->ply > 0 && (ss - 1)->current && (ss - 1)->movedPiece) {
    prevPc = (ss - 1)->movedPiece;
    prevTo = (ss - 1)->current.to();
    cm = countermove[prevPc][prevTo];
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
      if (prevPc) m->score += contHistory[prevPc][prevTo][mv.to()] / 4;
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
  if (ss->ply >= MAX_PLY - 1) return eval_pos(pos, ss);

  if (pos.is_draw(ss->ply))
    return VALUE_DRAW;

  const bool inCheckQS = pos.checkers();
  Value stand = VALUE_NONE;
  // Stand-pat is illegal while in check — must search evasions (or return mate).
  if (!inCheckQS) {
    stand = eval_pos(pos, ss);
    if (stand >= beta) return stand;
    if (stand > alpha) alpha = stand;
  }

  ExtMove moves[MAX_MOVES];
  ExtMove* end;
  if (inCheckQS) {
    end = generate<LEGAL>(pos, moves);
    if (moves == end) return mated_in(ss->ply);
  } else {
    end = generate<CAPTURES>(pos, moves);
    ExtMove* n = moves;
    for (ExtMove* m = moves; m != end; ++m)
      if (pos.is_legal(m->move)) *n++ = *m;
    // Quiet promotions are horizon events — always include in qsearch.
    ExtMove quiets[MAX_MOVES];
    ExtMove* qend = generate<QUIETS>(pos, quiets);
    ExtMove checkCands[MAX_MOVES];
    ExtMove* checkEnd = checkCands;
    for (ExtMove* m = quiets; m != qend; ++m) {
      if (!pos.is_legal(m->move)) continue;
      if (m->move.type() == PROMOTION) {
        *n++ = *m;
        continue;
      }
      if (pos.gives_check(m->move) && pos.see_ge(m->move, 0))
        *checkEnd++ = *m;
    }
    // Prefer higher-SEE / better-ordered checks: score then take top 8
    order_moves(pos, checkCands, checkEnd, MOVE_NONE, ss);
    int checksAdded = 0;
    for (ExtMove* m = checkCands; m != checkEnd && checksAdded < 8; ++m, ++checksAdded)
      *n++ = *m;
    end = n;
  }

  order_moves(pos, moves, end, MOVE_NONE, ss);
  StateInfo st;

  for (ExtMove* em = moves; em != end; ++em) {
    Move m = em->move;
    if (!inCheckQS) {
      int captureVal = m.type() == EN_PASSANT ? 100
                     : (m.type() == PROMOTION ? 800 : 0);
      if (m.type() == PROMOTION)
        captureVal += piece_value(m.promotion_type()) - piece_value(PAWN);
      if (pos.piece_on(m.to()))
        captureVal = piece_value(type_of(pos.piece_on(m.to())))
                   + (m.type() == PROMOTION ? piece_value(m.promotion_type()) - piece_value(PAWN) : 0);
      if (stand + captureVal + 150 < alpha) continue;
      if (m.type() != PROMOTION && !pos.see_ge(m, 0)) continue;
    }

    if (useNnueAcc) nnue().do_move((ss + 1)->acc, ss->acc, pos, m);
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
    if (ss->ply >= MAX_PLY - 1) return VALUE_DRAW;
  }

  alpha = std::max(alpha, mated_in(ss->ply));
  beta = std::min(beta, mate_in(ss->ply + 1));
  if (alpha >= beta) return alpha;

  if (depth <= 0)
    return qsearch(pos, ss, alpha, beta);

  info.seldepth = std::max(info.seldepth, ss->ply);

  // Draws before TT: a TT score must not override an actual repetition / 50-move draw.
  if (!rootNode && pos.is_draw(ss->ply))
    return VALUE_DRAW;

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
    eval = ss->staticEval = ttHit && tte->eval != int16_t(VALUE_NONE) ? Value(tte->eval) : eval_pos(pos, ss);
  }

  const bool improving = !inCheck && ss->ply >= 2 &&
      (ss - 2)->staticEval != VALUE_NONE && eval >(ss - 2)->staticEval;

  // Reverse futility pruning
  if (!pvNode && !inCheck && depth <= 7 &&
      eval - ReverseFutilityMargin * depth - (improving ? 0 : 35) >= beta)
    return eval;

  // Razoring
  if (!pvNode && !inCheck && depth <= 3 && eval + RazorMargin * depth < alpha)
    return qsearch(pos, ss, alpha, beta);

  // Null move
  if (!pvNode && !inCheck && depth >= 2 && eval >= beta &&
      pos.non_pawn_material(pos.side_to_move()) &&
      (ss - 1)->current != MOVE_NULL &&
      ss->staticEval >= beta - 20 * depth + (improving ? 180 : 220)) {
    StateInfo st;
    int R = 3 + depth / 3 + std::min(2, (eval - beta) / 220);
    if (useNnueAcc) (ss + 1)->acc.copy_from(ss->acc);
    pos.do_null_move(st);
    Value nullScore = -search_node(pos, ss + 1, -beta, -beta + 1, depth - R, !cutNode);
    pos.undo_null_move();
    if (info.stop) return alpha;
    if (nullScore >= beta)
      return nullScore >= VALUE_MATE_IN_MAX_PLY ? beta : nullScore;
  }

  // ProbCut disabled: earlier aggressive variants regressed Elo 2000; revisit with SPRT.

  // Internal iterative deepening: shallow search to get a TT move (PV only)
  if (pvNode && !ttMove && depth >= 6) {
    search_node(pos, ss, alpha, beta, depth - 2, false);
    if (info.stop) return alpha;
    tte = tt.probe(pos.key(), ttHit);
    ttMove = ttHit ? tte->move : MOVE_NONE;
    ttValue = ttHit ? value_from_tt(Value(tte->score), ss->ply) : VALUE_NONE;
  } else if (!ttMove && depth >= 7) {
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

  for (ExtMove* em = moves; em != end; ++em) {
    Move m = em->move;
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

    // Quiet moves that hang material — only shallow; deeper SEE pruning was too blunt.
    if (!rootNode && !pvNode && !capture && !givesCheck && depth <= 3 && moveCount > 1 &&
        !pos.see_ge(m, 0))
      continue;

    Depth newDepth = depth - 1;
    int extension = 0;
    // Extend safe checks only at shallow-to-medium depth (was tautological depth<=6?1:1).
    if (!rootNode && givesCheck && depth <= 8 && pos.see_ge(m, 0))
      extension = 1;
    if (!rootNode && ss->ply >= 1 && (ss - 1)->current &&
        m.to() == (ss - 1)->current.to() && capture)
      extension = std::max(extension, 1);
    // Pseudo-singular removed: a near-beta LOWER TT hit is not proof of singularity.

    Depth reduction = 0;
    if (depth >= 3 && moveCount > 1 + pvNode && !capture && !givesCheck) {
      reduction = Depth(0.65 + std::log(double(depth)) * std::log(double(moveCount)) / 2.50);
      if (cutNode) ++reduction;
      if (!improving) ++reduction;
      if (ss->killers[0] == m || ss->killers[1] == m) reduction = std::max(0, reduction - 1);
      int hist = history[pos.side_to_move()][m.from()][m.to()];
      if (ss->ply > 0 && (ss - 1)->movedPiece)
        hist += contHistory[(ss - 1)->movedPiece][(ss - 1)->current.to()][m.to()] / 4;
      if (hist > 3000) reduction = std::max(0, reduction - 1);
      if (hist < -1500) ++reduction;
      reduction = std::clamp(reduction, 0, newDepth - 1 + extension);
    }

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
            if (ss->ply > 0 && (ss - 1)->movedPiece) {
              Piece prev = (ss - 1)->movedPiece;
              Square prevTo = (ss - 1)->current.to();
              countermove[prev][prevTo] = m;
              int& ch = contHistory[prev][prevTo][m.to()];
              ch += bonus - ch * bonus / 16384;
            }
          } else if (pos.piece_on(m.to()) || m.type() == EN_PASSANT) {
            Piece attacker = pos.piece_on(m.from());
            int victim = m.type() == EN_PASSANT ? PAWN : type_of(pos.piece_on(m.to()));
            int& ch = captureHistory[attacker][m.to()][victim];
            int bonus = depth * depth;
            ch += bonus - ch * bonus / 16384;
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
  useNnueAcc = nnue_ready() && std::getenv("ADITYA_USE_NNUE") &&
               std::getenv("ADITYA_USE_NNUE")[0] == '1';

  // Match probe_book's ply-14 ceiling (was gated at ply 8, discarding deeper book).
  if (!limits.infinite && pos.game_ply() <= 14) {
    Move bookMove = probe_book(pos);
    if (bookMove) {
      std::cout << "info string book " << move_to_uci(bookMove) << std::endl;
      return bookMove;
    }
  }

  startTime = now_ms();
  allocatedTime = 0;
  if (limits.movetime > 0) {
    allocatedTime = std::max(8, limits.movetime - 10);
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
  (ss - 1)->current = MOVE_NULL;
  (ss - 1)->staticEval = VALUE_NONE;
  (ss - 2)->staticEval = VALUE_NONE;
  if (useNnueAcc) nnue().refresh(ss->acc, pos);

  int maxDepth = limits.depth > 0 ? limits.depth : MAX_PLY - 2;
  Value alpha = -VALUE_INFINITE, beta = VALUE_INFINITE;
  Value bestScore = 0;

  for (int depth = 1; depth <= maxDepth; ++depth) {
    if (depth >= 5) {
      alpha = bestScore - 28;
      beta = bestScore + 28;
    } else {
      alpha = -VALUE_INFINITE;
      beta = VALUE_INFINITE;
    }

    int delta = 28;
    while (true) {
      bestScore = search_node(pos, ss, alpha, beta, depth, false);
      if (info.stop) break;
      if (bestScore <= alpha) {
        beta = (alpha + beta) / 2;
        alpha = bestScore - delta;
        delta += delta / 2;
        // Fail low: spend a bit more time
        if (allocatedTime > 0) allocatedTime = std::min(allocatedTime + allocatedTime / 8,
            limits.movetime > 0 ? std::max<int64_t>(8, limits.movetime - 5)
                                : allocatedTime * 2);
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
    if (allocatedTime > 0 && (now_ms() - startTime) > allocatedTime * 95 / 100) break;
    if (std::abs(bestScore) > VALUE_MATE_IN_MAX_PLY) break;
  }

  if (!bestRootMove) {
    MoveListWrapper list(pos);
    if (list.size()) bestRootMove = list.begin()->move;
  }
  return bestRootMove;
}

} // namespace ah
