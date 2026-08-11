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

} // namespace ah
