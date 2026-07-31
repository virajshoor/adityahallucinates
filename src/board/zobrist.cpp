#include "board/zobrist.hpp"
#include <random>

namespace ah {
namespace Zobrist {

Key psq[PIECE_NB][SQUARE_NB];
Key enpassant[FILE_NB];
Key castling[16];
Key side;

void init() {
  std::mt19937_64 rng(0xA1D1A1ULL);
  for (int pc = 0; pc < PIECE_NB; ++pc)
    for (Square s = SQ_A1; s < SQUARE_NB; ++s)
      psq[pc][s] = rng();
  for (File f = FILE_A; f < FILE_NB; ++f)
    enpassant[f] = rng();
  for (int i = 0; i < 16; ++i)
    castling[i] = rng();
  side = rng();
}

} // namespace Zobrist
} // namespace ah
