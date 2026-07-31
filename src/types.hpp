#pragma once

#include <cstdint>
#include <string>
#include <cassert>
#include <algorithm>
#include <array>
#include <cstring>

namespace ah {

using Bitboard = uint64_t;
using Key = uint64_t;
using Score = int16_t;
using Depth = int;
using Value = int;

constexpr int MAX_PLY = 128;
constexpr int MAX_MOVES = 256;
constexpr Value VALUE_INFINITE = 32000;
constexpr Value VALUE_MATE = 31000;
constexpr Value VALUE_MATE_IN_MAX_PLY = VALUE_MATE - MAX_PLY;
constexpr Value VALUE_DRAW = 0;
constexpr Value VALUE_NONE = 32001;

enum Color : int { WHITE = 0, BLACK = 1, COLOR_NB = 2 };
enum PieceType : int {
  NO_PIECE_TYPE = 0, PAWN = 1, KNIGHT = 2, BISHOP = 3, ROOK = 4, QUEEN = 5, KING = 6, PIECE_TYPE_NB = 7
};
enum Piece : int {
  NO_PIECE = 0,
  W_PAWN = 1, W_KNIGHT = 2, W_BISHOP = 3, W_ROOK = 4, W_QUEEN = 5, W_KING = 6,
  B_PAWN = 9, B_KNIGHT = 10, B_BISHOP = 11, B_ROOK = 12, B_QUEEN = 13, B_KING = 14,
  PIECE_NB = 16
};
enum Square : int {
  SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
  SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
  SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
  SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
  SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
  SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
  SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
  SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
  SQ_NONE = 64, SQUARE_NB = 64
};
enum File : int { FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H, FILE_NB };
enum Rank : int { RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8, RANK_NB };
enum CastlingRights : int {
  NO_CASTLING = 0,
  WHITE_OO = 1, WHITE_OOO = 2, BLACK_OO = 4, BLACK_OOO = 8,
  KING_SIDE = WHITE_OO | BLACK_OO,
  QUEEN_SIDE = WHITE_OOO | BLACK_OOO,
  WHITE_CASTLING = WHITE_OO | WHITE_OOO,
  BLACK_CASTLING = BLACK_OO | BLACK_OOO,
  ANY_CASTLING = WHITE_CASTLING | BLACK_CASTLING
};
enum Direction : int {
  NORTH = 8, SOUTH = -8, EAST = 1, WEST = -1,
  NORTH_EAST = 9, NORTH_WEST = 7, SOUTH_EAST = -7, SOUTH_WEST = -9
};

constexpr Color operator~(Color c) { return Color(c ^ 1); }
constexpr Piece make_piece(Color c, PieceType pt) { return Piece((c << 3) | pt); }
constexpr PieceType type_of(Piece pc) { return PieceType(pc & 7); }
constexpr Color color_of(Piece pc) { return Color(pc >> 3); }
constexpr bool is_ok(Square s) { return s >= SQ_A1 && s < SQUARE_NB; }
constexpr File file_of(Square s) { return File(s & 7); }
constexpr Rank rank_of(Square s) { return Rank(s >> 3); }
constexpr Square make_square(File f, Rank r) { return Square((r << 3) | f); }
constexpr Square relative_square(Color c, Square s) { return Square(s ^ (c * 56)); }
constexpr Rank relative_rank(Color c, Rank r) { return Rank(r ^ (c * 7)); }
constexpr Rank relative_rank(Color c, Square s) { return relative_rank(c, rank_of(s)); }
constexpr Direction pawn_push(Color c) { return c == WHITE ? NORTH : SOUTH; }

constexpr Square& operator++(Square& s) { return s = Square(int(s) + 1); }
constexpr File& operator++(File& f) { return f = File(int(f) + 1); }
constexpr Rank& operator++(Rank& r) { return r = Rank(int(r) + 1); }
constexpr Square& operator--(Square& s) { return s = Square(int(s) - 1); }
constexpr File& operator--(File& f) { return f = File(int(f) - 1); }
constexpr Rank& operator--(Rank& r) { return r = Rank(int(r) - 1); }
constexpr Square operator+(Square s, int d) { return Square(int(s) + d); }
constexpr Square operator-(Square s, int d) { return Square(int(s) - d); }
constexpr Square operator+(Square s, Direction d) { return Square(int(s) + int(d)); }
constexpr Square operator-(Square s, Direction d) { return Square(int(s) - int(d)); }

constexpr Bitboard square_bb(Square s) { return 1ULL << s; }
constexpr Bitboard file_bb(File f) { return 0x0101010101010101ULL << f; }
constexpr Bitboard rank_bb(Rank r) { return 0xFFULL << (r * 8); }

inline int popcount(Bitboard b) { return __builtin_popcountll(b); }
inline Square lsb(Bitboard b) { return Square(__builtin_ctzll(b)); }
inline Square pop_lsb(Bitboard& b) {
  Square s = lsb(b);
  b &= b - 1;
  return s;
}
inline bool more_than_one(Bitboard b) { return b & (b - 1); }

// Move encoding: from(6) | to(6) | promo(3) | special(2)  -> 16 bits
// special: 0 normal, 1 promotion, 2 en passant, 3 castling
enum MoveType { NORMAL = 0, PROMOTION = 1, EN_PASSANT = 2, CASTLING = 3 };

struct Move {
  uint16_t data = 0;
  constexpr Move() = default;
  constexpr explicit Move(uint16_t d) : data(d) {}
  constexpr Move(Square from, Square to, MoveType mt = NORMAL, PieceType promo = KNIGHT)
      : data(uint16_t(from | (to << 6) | ((promo - KNIGHT) << 12) | (mt << 14))) {}

  constexpr Square from() const { return Square(data & 0x3F); }
  constexpr Square to() const { return Square((data >> 6) & 0x3F); }
  constexpr PieceType promotion_type() const { return PieceType(((data >> 12) & 3) + KNIGHT); }
  constexpr MoveType type() const { return MoveType((data >> 14) & 3); }
  constexpr bool is_null() const { return data == 0; }
  constexpr bool operator==(Move m) const { return data == m.data; }
  constexpr bool operator!=(Move m) const { return data != m.data; }
  constexpr explicit operator bool() const { return data != 0; }
};

constexpr Move MOVE_NONE{};
constexpr Move MOVE_NULL{65}; // sentinel unused encoding

struct ExtMove {
  Move move;
  int score = 0;
  bool operator<(const ExtMove& o) const { return score > o.score; } // higher first
};

using MoveList = std::array<ExtMove, MAX_MOVES>;

inline std::string square_to_string(Square s) {
  return std::string{char('a' + file_of(s)), char('1' + rank_of(s))};
}

inline std::string move_to_uci(Move m) {
  if (!m) return "0000";
  std::string s = square_to_string(m.from()) + square_to_string(m.to());
  if (m.type() == PROMOTION) {
    constexpr char promo[] = {'n', 'b', 'r', 'q'};
    s += promo[m.promotion_type() - KNIGHT];
  }
  return s;
}

inline Value mate_in(int ply) { return VALUE_MATE - ply; }
inline Value mated_in(int ply) { return -VALUE_MATE + ply; }

} // namespace ah
