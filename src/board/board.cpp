#include "board/board.hpp"
#include <sstream>
#include <cctype>
#include <stdexcept>
#include <algorithm>

namespace ah {

namespace {
constexpr int PieceValue[PIECE_TYPE_NB] = {0, 100, 320, 330, 500, 900, 0};
const char* StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

Bitboard between_sq(Square s1, Square s2) { return Bitboards::BetweenBB[s1][s2]; }
} // namespace

void Position::put_piece(Piece pc, Square s) {
  board[s] = pc;
  by_type[ALL_PIECES] |= s;
  by_type[type_of(pc)] |= s;
  by_color[color_of(pc)] |= s;
  piece_count[pc]++;
  if (type_of(pc) != PAWN && type_of(pc) != KING)
    npm[color_of(pc)] += PieceValue[type_of(pc)];
}

void Position::remove_piece(Square s) {
  Piece pc = board[s];
  by_type[ALL_PIECES] ^= s;
  by_type[type_of(pc)] ^= s;
  by_color[color_of(pc)] ^= s;
  board[s] = NO_PIECE;
  piece_count[pc]--;
  if (type_of(pc) != PAWN && type_of(pc) != KING)
    npm[color_of(pc)] -= PieceValue[type_of(pc)];
}

void Position::move_piece(Square from, Square to) {
  Piece pc = board[from];
  Bitboard ft = square_bb(from) | square_bb(to);
  by_type[ALL_PIECES] ^= ft;
  by_type[type_of(pc)] ^= ft;
  by_color[color_of(pc)] ^= ft;
  board[from] = NO_PIECE;
  board[to] = pc;
}

void Position::set_castling_right(Color c, Square rfrom) {
  Square ksq = king_square(c);
  bool kingSide = file_of(rfrom) > file_of(ksq);
  CastlingRights cr = c == WHITE ? (kingSide ? WHITE_OO : WHITE_OOO)
                                 : (kingSide ? BLACK_OO : BLACK_OOO);
  Square rto = make_square(kingSide ? FILE_F : FILE_D, rank_of(ksq));
  Square kto = make_square(kingSide ? FILE_G : FILE_C, rank_of(ksq));

  st->castling |= cr;
  castling_rights_mask[ksq] |= cr;
  castling_rights_mask[rfrom] |= cr;
  castling_rook_square[c][kingSide ? 0 : 1] = rfrom;

  Bitboard path = 0;
  for (int i = std::min((int)rfrom, (int)rto); i <= std::max((int)rfrom, (int)rto); ++i) {
    Square s = Square(i);
    if (s != ksq && s != rfrom) path |= s;
  }
  for (int i = std::min((int)ksq, (int)kto); i <= std::max((int)ksq, (int)kto); ++i) {
    Square s = Square(i);
    if (s != ksq && s != rfrom) path |= s;
  }
  castling_path[c][kingSide ? 0 : 1] = path;
}

void Position::set_check_info() {
  st->blockers_for_king[WHITE] = st->blockers_for_king[BLACK] = 0;
  st->pinners[WHITE] = st->pinners[BLACK] = 0;

  for (Color c : {WHITE, BLACK}) {
    Square ksq = king_square(c);
    Bitboard snipers =
        (attacks_bb(ROOK, ksq, 0) & (pieces(~c, QUEEN) | pieces(~c, ROOK))) |
        (attacks_bb(BISHOP, ksq, 0) & (pieces(~c, QUEEN) | pieces(~c, BISHOP)));
    Bitboard occupancy = pieces() ^ snipers;

    while (snipers) {
      Square sniperSq = pop_lsb(snipers);
      Bitboard b = between_sq(ksq, sniperSq) & occupancy;
      if (b && !more_than_one(b)) {
        st->blockers_for_king[c] |= b;
        if (b & pieces(c))
          st->pinners[~c] |= sniperSq;
      }
    }
  }
  st->checkers = attackers_to(king_square(side)) & pieces(~side);
}

Key Position::compute_key() const {
  Key k = 0;
  Bitboard b = pieces();
  while (b) {
    Square s = pop_lsb(b);
    k ^= Zobrist::psq[piece_on(s)][s];
  }
  if (st->ep_square != SQ_NONE)
    k ^= Zobrist::enpassant[file_of(st->ep_square)];
  k ^= Zobrist::castling[st->castling & 15];
  if (side == BLACK) k ^= Zobrist::side;
  return k;
}

Key Position::compute_pawn_key() const {
  Key k = 0;
  Bitboard b = pieces(PAWN);
  while (b) {
    Square s = pop_lsb(b);
    k ^= Zobrist::psq[piece_on(s)][s];
  }
  return k;
}

void Position::set_state() {
  st->key = compute_key();
  st->pawnKey = compute_pawn_key();
  set_check_info();
}

void Position::set(const std::string& fenStr, StateInfo& si) {
  std::memset(board, 0, sizeof(board));
  std::memset(by_type, 0, sizeof(by_type));
  std::memset(by_color, 0, sizeof(by_color));
  std::memset(piece_count, 0, sizeof(piece_count));
  std::memset(npm, 0, sizeof(npm));
  std::memset(castling_rights_mask, 0, sizeof(castling_rights_mask));
  std::memset(castling_rook_square, 0, sizeof(castling_rook_square));
  std::memset(castling_path, 0, sizeof(castling_path));
  for (int i = 0; i < SQUARE_NB; ++i) board[i] = NO_PIECE;

  st = &si;
  std::memset(st, 0, sizeof(StateInfo));

  std::istringstream ss(fenStr);
  ss >> std::noskipws;
  char ch;
  Square sq = SQ_A8;

  while ((ss >> ch) && !isspace(ch)) {
    if (ch == '/') {
      sq = Square(int(sq) - 16);
    } else if (isdigit(ch)) {
      sq = Square(int(sq) + (ch - '0'));
    } else {
      Color c = isupper(ch) ? WHITE : BLACK;
      PieceType pt;
      switch (tolower(ch)) {
        case 'p': pt = PAWN; break;
        case 'n': pt = KNIGHT; break;
        case 'b': pt = BISHOP; break;
        case 'r': pt = ROOK; break;
        case 'q': pt = QUEEN; break;
        case 'k': pt = KING; break;
        default: throw std::runtime_error("bad fen piece");
      }
      put_piece(make_piece(c, pt), sq);
      sq = Square(int(sq) + 1);
    }
  }

  ss >> ch;
  side = (ch == 'w') ? WHITE : BLACK;
  ss >> ch;
  st->castling = NO_CASTLING;
  while ((ss >> ch) && !isspace(ch)) {
    Square rsq = SQ_NONE;
    Color c = isupper(ch) ? WHITE : BLACK;
    Rank r = relative_rank(c, RANK_1);
    if (ch == 'K' || ch == 'k') rsq = make_square(FILE_H, r);
    else if (ch == 'Q' || ch == 'q') rsq = make_square(FILE_A, r);
    else if (ch >= 'A' && ch <= 'H') rsq = make_square(File(ch - 'A'), RANK_1);
    else if (ch >= 'a' && ch <= 'h') rsq = make_square(File(ch - 'a'), RANK_8);
    if (rsq != SQ_NONE && (pieces(c, ROOK) & rsq))
      set_castling_right(c, rsq);
  }

  std::string ep;
  ss >> std::skipws >> ep;
  st->ep_square = SQ_NONE;
  if (ep != "-" && ep.size() == 2)
    st->ep_square = make_square(File(ep[0] - 'a'), Rank(ep[1] - '1'));

  int fifty = 0, full = 1;
  ss >> fifty >> full;
  st->rule50 = fifty;
  gamePly = std::max(2 * (full - 1), 0) + (side == BLACK ? 1 : 0);
  set_state();
}

void Position::set_startpos(StateInfo& si) { set(StartFEN, si); }

std::string Position::fen() const {
  std::ostringstream ss;
  for (int r = RANK_8; r >= RANK_1; --r) {
    int empty = 0;
    for (int f = FILE_A; f <= FILE_H; ++f) {
      Piece pc = piece_on(make_square(File(f), Rank(r)));
      if (pc == NO_PIECE) ++empty;
      else {
        if (empty) { ss << empty; empty = 0; }
        constexpr char chars[] = " PNBRQK  pnbrqk";
        ss << chars[pc];
      }
    }
    if (empty) ss << empty;
    if (r > RANK_1) ss << '/';
  }
  ss << (side == WHITE ? " w " : " b ");
  if (st->castling & WHITE_OO) ss << 'K';
  if (st->castling & WHITE_OOO) ss << 'Q';
  if (st->castling & BLACK_OO) ss << 'k';
  if (st->castling & BLACK_OOO) ss << 'q';
  if (!(st->castling & ANY_CASTLING)) ss << '-';
  ss << ' ';
  if (st->ep_square == SQ_NONE) ss << '-';
  else ss << square_to_string(st->ep_square);
  ss << ' ' << st->rule50 << ' ' << (1 + (gamePly - (side == BLACK ? 1 : 0)) / 2);
  return ss.str();
}

Bitboard Position::attackers_to(Square s, Bitboard occupied) const {
  return (Bitboards::PawnAttacks[WHITE][s] & pieces(BLACK, PAWN)) |
         (Bitboards::PawnAttacks[BLACK][s] & pieces(WHITE, PAWN)) |
         (Bitboards::PseudoAttacks[KNIGHT][s] & pieces(KNIGHT)) |
         (attacks_bb(BISHOP, s, occupied) & (pieces(BISHOP) | pieces(QUEEN))) |
         (attacks_bb(ROOK, s, occupied) & (pieces(ROOK) | pieces(QUEEN))) |
         (Bitboards::PseudoAttacks[KING][s] & pieces(KING));
}

Bitboard Position::attackers_to(Square s, Color c) const {
  return attackers_to(s, pieces()) & pieces(c);
}

void Position::do_move(Move m, StateInfo& new_st) {
  assert(m);
  Key k = st->key ^ Zobrist::side;
  Key pk = st->pawnKey;

  std::memcpy(&new_st, st, sizeof(StateInfo));
  new_st.previous = st;
  st = &new_st;

  ++gamePly;
  ++st->rule50;
  ++st->plies_from_null;
  st->captured = NO_PIECE;

  Color us = side;
  Color them = ~us;
  Square from = m.from();
  Square to = m.to();
  Piece pc = piece_on(from);
  Piece captured = m.type() == EN_PASSANT ? make_piece(them, PAWN) : piece_on(to);

  if (m.type() == CASTLING) {
    bool kingSide = to > from;
    Square rfrom = to; // UCI encodes king destination as G/C; Stockfish style uses rook square as `to`
    // Our move encoding for castling: from=king, to=king destination (g1/c1/g8/c8)
    Square kto = to;
    Square rto = make_square(kingSide ? FILE_F : FILE_D, rank_of(from));
    rfrom = castling_rook_square[us][kingSide ? 0 : 1];

    k ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][kto];
    k ^= Zobrist::psq[make_piece(us, ROOK)][rfrom] ^ Zobrist::psq[make_piece(us, ROOK)][rto];
    move_piece(from, kto);
    move_piece(rfrom, rto);
    captured = NO_PIECE;
  } else {
    if (captured) {
      Square capsq = to;
      if (m.type() == EN_PASSANT)
        capsq = Square(to - pawn_push(us));
      k ^= Zobrist::psq[captured][capsq];
      if (type_of(captured) == PAWN)
        pk ^= Zobrist::psq[captured][capsq];
      remove_piece(capsq);
      st->captured = captured;
      st->rule50 = 0;
    }

    k ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];
    if (type_of(pc) == PAWN)
      pk ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];
    move_piece(from, to);

    if (m.type() == PROMOTION) {
      Piece promotion = make_piece(us, m.promotion_type());
      remove_piece(to);
      put_piece(promotion, to);
      k ^= Zobrist::psq[pc][to] ^ Zobrist::psq[promotion][to];
      pk ^= Zobrist::psq[pc][to]; // pawn leaves the board
    }
  }

  if (st->ep_square != SQ_NONE) {
    k ^= Zobrist::enpassant[file_of(st->ep_square)];
    st->ep_square = SQ_NONE;
  }

  if (type_of(pc) == PAWN) {
    st->rule50 = 0;
    if ((int(to) ^ int(from)) == 16 &&
        (Bitboards::PawnAttacks[us][Square((from + to) / 2)] & pieces(them, PAWN))) {
      st->ep_square = Square((from + to) / 2);
      k ^= Zobrist::enpassant[file_of(st->ep_square)];
    }
  }

  k ^= Zobrist::castling[st->castling & 15];
  st->castling &= ~(castling_rights_mask[from] | castling_rights_mask[to]);
  k ^= Zobrist::castling[st->castling & 15];

  st->key = k;
  st->pawnKey = pk;
  side = them;
  set_check_info();
}

void Position::undo_move(Move m) {
  side = ~side;
  Color us = side;

  Square from = m.from();
  Square to = m.to();

  if (m.type() == PROMOTION) {
    remove_piece(to);
    put_piece(make_piece(us, PAWN), to);
  }

  if (m.type() == CASTLING) {
    bool kingSide = to > from;
    Square rto = make_square(kingSide ? FILE_F : FILE_D, rank_of(from));
    Square rfrom = castling_rook_square[us][kingSide ? 0 : 1];
    move_piece(to, from);
    move_piece(rto, rfrom);
  } else {
    move_piece(to, from);
    if (st->captured) {
      Square capsq = to;
      if (m.type() == EN_PASSANT)
        capsq = Square(to - pawn_push(us));
      put_piece(st->captured, capsq);
    }
  }

  st = st->previous;
  --gamePly;
}

void Position::do_null_move(StateInfo& new_st) {
  std::memcpy(&new_st, st, sizeof(StateInfo));
  new_st.previous = st;
  st = &new_st;

  if (st->ep_square != SQ_NONE) {
    st->key ^= Zobrist::enpassant[file_of(st->ep_square)];
    st->ep_square = SQ_NONE;
  }
  st->key ^= Zobrist::side;
  ++st->rule50;
  st->plies_from_null = 0;
  side = ~side;
  set_check_info();
}

void Position::undo_null_move() {
  st = st->previous;
  side = ~side;
}

bool Position::is_pseudo_legal(Move m) const {
  Color us = side;
  Square from = m.from();
  Square to = m.to();
  Piece pc = piece_on(from);

  if (m.type() == CASTLING) {
    CastlingRights cr = (to > from)
        ? (us == WHITE ? WHITE_OO : BLACK_OO)
        : (us == WHITE ? WHITE_OOO : BLACK_OOO);
    return can_castle(cr) && !(castling_path[us][to > from ? 0 : 1] & pieces()) && !checkers();
  }

  if (pc == NO_PIECE || color_of(pc) != us) return false;
  if (pieces(us) & to) return false;

  if (type_of(pc) == PAWN) {
    if (m.type() == PROMOTION && relative_rank(us, to) != RANK_8) return false;
    if (m.type() != PROMOTION && relative_rank(us, to) == RANK_8) return false;

    Bitboard dst = square_bb(to);
    if (m.type() == EN_PASSANT)
      return to == ep_square() && (Bitboards::PawnAttacks[us][from] & dst);
    if (Bitboards::PawnAttacks[us][from] & dst & pieces(~us)) return true;
    if (m.type() == EN_PASSANT) return false;
    Square up = Square(from + pawn_push(us));
    if (to == up && !piece_on(up)) return true;
    if (relative_rank(us, from) == RANK_2 && to == Square(from + 2 * pawn_push(us)) &&
        !piece_on(up) && !piece_on(to))
      return true;
    return false;
  }

  if (m.type() == PROMOTION || m.type() == EN_PASSANT) return false;

  return attacks_bb(type_of(pc), from, pieces()) & to;
}

bool Position::is_legal(Move m) const {
  if (!is_pseudo_legal(m)) return false;

  Color us = side;
  Square from = m.from();
  Square to = m.to();
  Square ksq = king_square(us);

  if (m.type() == EN_PASSANT) {
    Square capsq = Square(to - pawn_push(us));
    Bitboard occupied = (pieces() ^ from ^ capsq) | to;
    return !(attacks_bb(ROOK, ksq, occupied) & (pieces(~us, QUEEN) | pieces(~us, ROOK))) &&
           !(attacks_bb(BISHOP, ksq, occupied) & (pieces(~us, QUEEN) | pieces(~us, BISHOP)));
  }

  if (m.type() == CASTLING) {
    Direction step = to > from ? EAST : WEST;
    for (Square s = from; s != to; s = Square(s + step))
      if (attackers_to(s, ~us)) return false;
    return !attackers_to(to, ~us);
  }

  if (type_of(piece_on(from)) == KING)
    return !(attackers_to(to, pieces() ^ from) & pieces(~us));

  // Moving a pinned piece must stay on the pin ray
  return !(st->blockers_for_king[us] & from) ||
         (Bitboards::LineBB[from][to] & ksq);
}

bool Position::gives_check(Move m) const {
  // Approximate: make move would be expensive; use attack geometry
  Square from = m.from();
  Square to = m.to();
  PieceType pt = type_of(piece_on(from));
  Color us = side;
  Square ksq = king_square(~us);

  // Pawns: PseudoAttacks[PAWN] is empty — use pawn attack tables directly.
  if (pt == PAWN) {
    if (Bitboards::PawnAttacks[us][to] & ksq) return true;
  } else if (Bitboards::PseudoAttacks[pt][to] & ksq) {
    if (pt == KNIGHT || pt == KING) {
      return true;
    } else if (attacks_bb(pt, to, pieces() ^ from) & ksq)
      return true;
  }

  // Discovery
  Bitboard blockers = st->blockers_for_king[~us];
  if ((blockers & from) && !(Bitboards::LineBB[from][ksq] & to))
    return true;

  if (m.type() == PROMOTION) {
    if (attacks_bb(m.promotion_type(), to, pieces() ^ from) & ksq) return true;
  }
  if (m.type() == EN_PASSANT) {
    Square capsq = Square(to - pawn_push(us));
    Bitboard occupied = (pieces() ^ from ^ capsq) | to;
    return (attacks_bb(ROOK, ksq, occupied) & (pieces(us, QUEEN) | pieces(us, ROOK))) ||
           (attacks_bb(BISHOP, ksq, occupied) & (pieces(us, QUEEN) | pieces(us, BISHOP)));
  }
  if (m.type() == CASTLING) {
    bool kingSide = to > from;
    Square rto = make_square(kingSide ? FILE_F : FILE_D, rank_of(from));
    return attacks_bb(ROOK, rto, pieces() ^ from ^ castling_rook_square[us][kingSide ? 0 : 1] ^ to) & ksq;
  }
  return false;
}

bool Position::see_ge(Move m, int threshold) const {
  if (m.type() == CASTLING) return 0 >= threshold;

  Square from = m.from(), to = m.to();
  // Use mover color from the piece — critical for threat eval of the non-STM side.
  Color mover = color_of(piece_on(from));

  int swap = PieceValue[type_of(piece_on(to))] - threshold;
  if (m.type() == EN_PASSANT) swap = PieceValue[PAWN] - threshold;
  if (m.type() == PROMOTION)
    swap += PieceValue[m.promotion_type()] - PieceValue[PAWN];
  if (swap < 0) return false;

  // After the move, opponent faces the piece now on `to` (promoted type if any).
  PieceType nextVictim = m.type() == PROMOTION ? m.promotion_type() : type_of(piece_on(from));
  swap = PieceValue[nextVictim] - swap;
  if (swap <= 0) return true;

  Bitboard occupied = pieces() ^ from ^ to;
  if (m.type() == EN_PASSANT) {
    Square cap = Square(int(to) - int(pawn_push(mover)));
    occupied ^= cap;
  }

  Color stm = mover;
  Bitboard attackers = attackers_to(to, occupied);
  Bitboard stmAttackers, bb;
  int res = 1;
  Square ksq;

  for (int seePlies = 0; seePlies < 32; ++seePlies) {
    stm = ~stm;
    attackers &= occupied;

    // Pinned pieces cannot recapture unless capturing along the pin ray.
    stmAttackers = attackers & pieces(stm);
    if (st->pinners[~stm] & occupied) {
      ksq = king_square(stm);
      stmAttackers &= ~st->blockers_for_king[stm] | Bitboards::LineBB[ksq][to];
    }
    if (!stmAttackers) break;

    res ^= 1;
    if ((bb = stmAttackers & pieces(PAWN))) {
      if ((swap = PieceValue[PAWN] - swap) < res) break;
      occupied ^= lsb(bb);
      attackers |= attacks_bb(BISHOP, to, occupied) & (pieces(BISHOP) | pieces(QUEEN));
    } else if ((bb = stmAttackers & pieces(KNIGHT))) {
      if ((swap = PieceValue[KNIGHT] - swap) < res) break;
      occupied ^= lsb(bb);
    } else if ((bb = stmAttackers & pieces(BISHOP))) {
      if ((swap = PieceValue[BISHOP] - swap) < res) break;
      occupied ^= lsb(bb);
      attackers |= attacks_bb(BISHOP, to, occupied) & (pieces(BISHOP) | pieces(QUEEN));
    } else if ((bb = stmAttackers & pieces(ROOK))) {
      if ((swap = PieceValue[ROOK] - swap) < res) break;
      occupied ^= lsb(bb);
      attackers |= attacks_bb(ROOK, to, occupied) & (pieces(ROOK) | pieces(QUEEN));
    } else if ((bb = stmAttackers & pieces(QUEEN))) {
      if ((swap = PieceValue[QUEEN] - swap) < res) break;
      occupied ^= lsb(bb);
      attackers |= (attacks_bb(BISHOP, to, occupied) & (pieces(BISHOP) | pieces(QUEEN))) |
                   (attacks_bb(ROOK, to, occupied) & (pieces(ROOK) | pieces(QUEEN)));
    } else { // king
      return (attackers & ~pieces(stm)) ? bool(res ^ 1) : bool(res);
    }
  }
  return bool(res);
}

bool Position::is_draw(int ply) const {
  // Fifty-move: never claim draw while in check — may be checkmate.
  if (st->rule50 >= 100 && !checkers()) return true;

  // Repetition (two-fold inside search)
  int end = std::min(st->rule50, st->plies_from_null);
  if (end < 4) return false;
  StateInfo* stp = st->previous->previous;
  for (int i = 4; i <= end; i += 2) {
    stp = stp->previous->previous;
    if (stp->key == st->key)
      return true;
  }
  (void)ply;
  return false;
}

bool Position::has_repeated() const {
  return is_draw(0);
}

} // namespace ah
