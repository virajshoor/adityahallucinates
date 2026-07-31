#pragma once

#include "types.hpp"
#include "board/bitboard.hpp"
#include "board/zobrist.hpp"
#include <string>
#include <vector>

namespace ah {

struct StateInfo {
  Key key = 0;
  Bitboard checkers = 0;
  Bitboard blockers_for_king[COLOR_NB] = {};
  Bitboard pinners[COLOR_NB] = {};
  Piece captured = NO_PIECE;
  int castling = NO_CASTLING;
  Square ep_square = SQ_NONE;
  int rule50 = 0;
  int plies_from_null = 0;
  StateInfo* previous = nullptr;
};

class Position {
public:
  void set(const std::string& fen, StateInfo& si);
  void set_startpos(StateInfo& si);
  std::string fen() const;

  Bitboard pieces() const { return by_type[ALL_PIECES]; }
  Bitboard pieces(PieceType pt) const { return by_type[pt]; }
  Bitboard pieces(Color c) const { return by_color[c]; }
  Bitboard pieces(Color c, PieceType pt) const { return by_color[c] & by_type[pt]; }
  Bitboard pieces(Color c, PieceType pt1, PieceType pt2) const {
    return by_color[c] & (by_type[pt1] | by_type[pt2]);
  }

  Piece piece_on(Square s) const { return board[s]; }
  Square ep_square() const { return st->ep_square; }
  int castling_rights() const { return st->castling; }
  bool can_castle(CastlingRights cr) const { return st->castling & cr; }
  Color side_to_move() const { return side; }
  Key key() const { return st->key; }
  int game_ply() const { return gamePly; }
  int rule50_count() const { return st->rule50; }
  Bitboard checkers() const { return st->checkers; }
  bool in_check() const { return st->checkers; }
  Square king_square(Color c) const { return lsb(pieces(c, KING)); }

  Bitboard attackers_to(Square s) const { return attackers_to(s, pieces()); }
  Bitboard attackers_to(Square s, Bitboard occupied) const;
  Bitboard attackers_to(Square s, Color c) const;

  void do_move(Move m, StateInfo& new_st);
  void undo_move(Move m);
  void do_null_move(StateInfo& new_st);
  void undo_null_move();

  bool is_legal(Move m) const;
  bool is_pseudo_legal(Move m) const;
  bool gives_check(Move m) const;
  bool see_ge(Move m, int threshold = 0) const;

  bool is_draw(int ply) const;
  bool has_repeated() const;

  int non_pawn_material(Color c) const { return npm[c]; }
  int non_pawn_material() const { return npm[WHITE] + npm[BLACK]; }

  static constexpr PieceType ALL_PIECES = PieceType(0);

private:
  void put_piece(Piece pc, Square s);
  void remove_piece(Square s);
  void move_piece(Square from, Square to);
  void set_castling_right(Color c, Square rfrom);
  void set_check_info();
  void set_state();
  Key compute_key() const;
  bool is_discovery_check_on_king(Color c, Bitboard blockers_removed) const;

  Piece board[SQUARE_NB] = {};
  Bitboard by_type[PIECE_TYPE_NB] = {};
  Bitboard by_color[COLOR_NB] = {};
  int piece_count[PIECE_NB] = {};
  int npm[COLOR_NB] = {};
  int castling_rights_mask[SQUARE_NB] = {};
  Square castling_rook_square[COLOR_NB][2] = {}; // [color][kingside=0/queenside=1]
  Bitboard castling_path[COLOR_NB][2] = {};

  Color side = WHITE;
  int gamePly = 0;
  StateInfo* st = nullptr;

  friend class MoveGenerator;
};

} // namespace ah
