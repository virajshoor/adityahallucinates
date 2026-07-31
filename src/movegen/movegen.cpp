#include "movegen/movegen.hpp"

namespace ah {

namespace {

template <Color Us, GenType Type>
ExtMove* generate_pawn_moves(const Position& pos, ExtMove* moveList, Bitboard target) {
  constexpr Color Them = ~Us;
  constexpr Direction Up = Us == WHITE ? NORTH : SOUTH;
  constexpr Direction UpUp = Us == WHITE ? Direction(16) : Direction(-16);
  constexpr Bitboard TRank7BB = Us == WHITE ? rank_bb(RANK_7) : rank_bb(RANK_2);
  constexpr Bitboard TRank3BB = Us == WHITE ? rank_bb(RANK_3) : rank_bb(RANK_6);

  const Bitboard pawnsOn7 = pos.pieces(Us, PAWN) & TRank7BB;
  const Bitboard pawnsNotOn7 = pos.pieces(Us, PAWN) & ~TRank7BB;
  const Bitboard empty = ~pos.pieces();

  if constexpr (Type != CAPTURES) {
    Bitboard single, doubles;
    if constexpr (Us == WHITE) {
      single = (pawnsNotOn7 << 8) & empty;
      doubles = ((single & TRank3BB) << 8) & empty;
    } else {
      single = (pawnsNotOn7 >> 8) & empty;
      doubles = ((single & TRank3BB) >> 8) & empty;
    }
    single &= target;
    doubles &= target;
    while (single) {
      Square to = pop_lsb(single);
      *moveList++ = {Move(Square(int(to) - int(Up)), to), 0};
    }
    while (doubles) {
      Square to = pop_lsb(doubles);
      *moveList++ = {Move(Square(int(to) - int(UpUp)), to), 0};
    }
  }

  if constexpr (Type != QUIETS) {
    Bitboard p = pawnsNotOn7;
    while (p) {
      Square from = pop_lsb(p);
      Bitboard atk = Bitboards::PawnAttacks[Us][from] & pos.pieces(Them) & target;
      while (atk) {
        Square to = pop_lsb(atk);
        *moveList++ = {Move(from, to), 0};
      }
    }

    if (pos.ep_square() != SQ_NONE) {
      Bitboard b = pawnsNotOn7 & Bitboards::PawnAttacks[Them][pos.ep_square()];
      // For evasions EP can still be legal even when ep-square is not in target
      while (b) {
        Square from = pop_lsb(b);
        if constexpr (Type == EVASIONS) {
          *moveList++ = {Move(from, pos.ep_square(), EN_PASSANT), 0};
        } else if (target & pos.ep_square() || target & Square(pos.ep_square() - Up)) {
          *moveList++ = {Move(from, pos.ep_square(), EN_PASSANT), 0};
        } else {
          *moveList++ = {Move(from, pos.ep_square(), EN_PASSANT), 0};
        }
      }
    }
  }

  if (pawnsOn7) {
    Bitboard p = pawnsOn7;
    while (p) {
      Square from = pop_lsb(p);
      Bitboard dest = 0;
      Square up = Square(int(from) + int(Up));
      if constexpr (Type != CAPTURES) {
        if ((empty & up) && (target & up)) dest |= up;
      }
      if constexpr (Type != QUIETS) {
        dest |= Bitboards::PawnAttacks[Us][from] & pos.pieces(Them) & target;
      }
      while (dest) {
        Square to = pop_lsb(dest);
        for (PieceType pt : {QUEEN, ROOK, BISHOP, KNIGHT})
          *moveList++ = {Move(from, to, PROMOTION, pt), 0};
      }
    }
  }

  return moveList;
}

template <Color Us, PieceType Pt>
ExtMove* generate_piece_moves(const Position& pos, ExtMove* moveList, Bitboard target) {
  Bitboard bb = pos.pieces(Us, Pt);
  while (bb) {
    Square from = pop_lsb(bb);
    Bitboard b = attacks_bb(Pt, from, pos.pieces()) & target;
    while (b)
      *moveList++ = {Move(from, pop_lsb(b)), 0};
  }
  return moveList;
}

template <Color Us, GenType Type>
ExtMove* generate_all(const Position& pos, ExtMove* moveList) {
  const Bitboard target = Type == CAPTURES ? pos.pieces(~Us)
                        : Type == QUIETS   ? ~pos.pieces()
                        : Type == EVASIONS ? (more_than_one(pos.checkers())
                                                 ? Bitboard(0)
                                                 : (Bitboards::BetweenBB[pos.king_square(Us)][lsb(pos.checkers())] |
                                                    pos.checkers()))
                                           : ~pos.pieces(Us);

  if constexpr (Type == EVASIONS) {
    if (!more_than_one(pos.checkers())) {
      moveList = generate_pawn_moves<Us, EVASIONS>(pos, moveList, target);
      moveList = generate_piece_moves<Us, KNIGHT>(pos, moveList, target);
      moveList = generate_piece_moves<Us, BISHOP>(pos, moveList, target);
      moveList = generate_piece_moves<Us, ROOK>(pos, moveList, target);
      moveList = generate_piece_moves<Us, QUEEN>(pos, moveList, target);
    }
  } else {
    moveList = generate_pawn_moves<Us, Type>(pos, moveList, target);
    moveList = generate_piece_moves<Us, KNIGHT>(pos, moveList, target);
    moveList = generate_piece_moves<Us, BISHOP>(pos, moveList, target);
    moveList = generate_piece_moves<Us, ROOK>(pos, moveList, target);
    moveList = generate_piece_moves<Us, QUEEN>(pos, moveList, target);
  }

  Square ksq = pos.king_square(Us);
  Bitboard b = Bitboards::PseudoAttacks[KING][ksq] & ~pos.pieces(Us);
  if constexpr (Type == CAPTURES)
    b &= pos.pieces(~Us);
  else if constexpr (Type == QUIETS)
    b &= ~pos.pieces();
  while (b)
    *moveList++ = {Move(ksq, pop_lsb(b)), 0};

  if constexpr (Type != CAPTURES && Type != EVASIONS) {
    if (!pos.checkers()) {
      if (pos.can_castle(Us == WHITE ? WHITE_OO : BLACK_OO))
        *moveList++ = {Move(ksq, make_square(FILE_G, rank_of(ksq)), CASTLING), 0};
      if (pos.can_castle(Us == WHITE ? WHITE_OOO : BLACK_OOO))
        *moveList++ = {Move(ksq, make_square(FILE_C, rank_of(ksq)), CASTLING), 0};
    }
  }
  return moveList;
}

} // namespace

template <GenType Type>
ExtMove* generate(const Position& pos, ExtMove* moveList) {
  ExtMove* cur = moveList;
  const bool inCheck = pos.checkers();
  Color us = pos.side_to_move();

  if (inCheck) {
    cur = us == WHITE ? generate_all<WHITE, EVASIONS>(pos, cur)
                      : generate_all<BLACK, EVASIONS>(pos, cur);
  } else if constexpr (Type == LEGAL) {
    cur = us == WHITE ? generate_all<WHITE, NON_EVASIONS>(pos, cur)
                      : generate_all<BLACK, NON_EVASIONS>(pos, cur);
  } else {
    cur = us == WHITE ? generate_all<WHITE, Type>(pos, cur)
                      : generate_all<BLACK, Type>(pos, cur);
  }

  if constexpr (Type == LEGAL) {
    ExtMove* end = cur;
    cur = moveList;
    for (ExtMove* i = moveList; i != end; ++i)
      if (pos.is_legal(i->move))
        *cur++ = *i;
  }
  return cur;
}

template ExtMove* generate<CAPTURES>(const Position&, ExtMove*);
template ExtMove* generate<QUIETS>(const Position&, ExtMove*);
template ExtMove* generate<EVASIONS>(const Position&, ExtMove*);
template ExtMove* generate<NON_EVASIONS>(const Position&, ExtMove*);
template ExtMove* generate<LEGAL>(const Position&, ExtMove*);

uint64_t perft(Position& pos, Depth depth) {
  MoveListWrapper list(pos);
  if (depth <= 0) return 1;
  if (depth == 1) return list.size();

  uint64_t nodes = 0;
  StateInfo st;
  for (const auto& em : list) {
    pos.do_move(em.move, st);
    nodes += perft(pos, depth - 1);
    pos.undo_move(em.move);
  }
  return nodes;
}

} // namespace ah
