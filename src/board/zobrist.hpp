#pragma once

#include "types.hpp"

namespace ah {

namespace Zobrist {
extern Key psq[PIECE_NB][SQUARE_NB];
extern Key enpassant[FILE_NB];
extern Key castling[16];
extern Key side;
void init();
} // namespace Zobrist

} // namespace ah
