#include "syzygy/syzygy.hpp"
#include "tbprobe.h"

namespace ah {

bool syzygy_init(const char* path) {
  if (!path || !path[0]) return false;
  return tb_init(path);
}

void syzygy_free() { tb_free(); }

int syzygy_max_pieces() { return int(TB_LARGEST); }

bool syzygy_probe_wdl(const Position& pos, int& wdl) {
  if (TB_LARGEST == 0) return false;
  if (popcount(pos.pieces()) > int(TB_LARGEST)) return false;
  if (pos.castling_rights()) return false;

  // Fathom WDL API requires rule50=0 here; 50-move draws are encoded as
  // cursed win / blessed loss in the WDL tables themselves.
  unsigned result = tb_probe_wdl(
      pos.pieces(WHITE), pos.pieces(BLACK),
      pos.pieces(KING), pos.pieces(QUEEN),
      pos.pieces(ROOK), pos.pieces(BISHOP),
      pos.pieces(KNIGHT), pos.pieces(PAWN),
      0,
      0,
      pos.ep_square() == SQ_NONE ? 0u : unsigned(pos.ep_square()),
      pos.side_to_move() == WHITE);

  if (result == TB_RESULT_FAILED) return false;

  switch (result) {
    case TB_WIN: wdl = 2; break;
    case TB_CURSED_WIN: wdl = 1; break;
    case TB_DRAW: wdl = 0; break;
    case TB_BLESSED_LOSS: wdl = -1; break;
    case TB_LOSS: wdl = -2; break;
    default: return false;
  }
  return true;
}

Move syzygy_probe_root(const Position& pos, int& wdl) {
  wdl = 0;
  if (TB_LARGEST == 0) return MOVE_NONE;
  if (popcount(pos.pieces()) > int(TB_LARGEST)) return MOVE_NONE;
  if (pos.castling_rights()) return MOVE_NONE;

  unsigned result = tb_probe_root(
      pos.pieces(WHITE), pos.pieces(BLACK),
      pos.pieces(KING), pos.pieces(QUEEN),
      pos.pieces(ROOK), pos.pieces(BISHOP),
      pos.pieces(KNIGHT), pos.pieces(PAWN),
      unsigned(pos.rule50_count()),
      0,
      pos.ep_square() == SQ_NONE ? 0u : unsigned(pos.ep_square()),
      pos.side_to_move() == WHITE,
      nullptr);

  if (result == TB_RESULT_FAILED || result == TB_RESULT_CHECKMATE ||
      result == TB_RESULT_STALEMATE)
    return MOVE_NONE;

  const unsigned rawWdl = TB_GET_WDL(result);
  switch (rawWdl) {
    case TB_WIN: wdl = 2; break;
    case TB_CURSED_WIN: wdl = 1; break;
    case TB_DRAW: wdl = 0; break;
    case TB_BLESSED_LOSS: wdl = -1; break;
    case TB_LOSS: wdl = -2; break;
    default: return MOVE_NONE;
  }
  // Only play DTZ for strict wins/losses. Cursed/blessed (wdl==±1) still
  // fifty-move draw under FIDE — playing them caused conversion failures.
  if (wdl != 2 && wdl != -2) return MOVE_NONE;

  Square from = Square(TB_GET_FROM(result));
  Square to = Square(TB_GET_TO(result));
  unsigned promo = TB_GET_PROMOTES(result);
  if (TB_GET_EP(result))
    return Move(from, to, EN_PASSANT);
  if (promo) {
    PieceType pt = QUEEN;
    switch (promo) {
      case TB_PROMOTES_QUEEN: pt = QUEEN; break;
      case TB_PROMOTES_ROOK: pt = ROOK; break;
      case TB_PROMOTES_BISHOP: pt = BISHOP; break;
      case TB_PROMOTES_KNIGHT: pt = KNIGHT; break;
      default: break;
    }
    return Move(from, to, PROMOTION, pt);
  }
  return Move(from, to);
}

} // namespace ah
