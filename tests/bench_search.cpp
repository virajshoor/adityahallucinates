#include "board/bitboard.hpp"
#include "board/zobrist.hpp"
#include "board/board.hpp"
#include "movegen/magics.hpp"
#include "search/search.hpp"
#include <iostream>

using namespace ah;

int main() {
  Bitboards::init();
  Zobrist::init();
  Magics::init();
  Position pos;
  StateInfo si;
  pos.set_startpos(si);
  Search search;
  search.set_hash(32);
  SearchLimits lim;
  lim.depth = 6;
  Move m = search.think(pos, lim);
  std::cout << "best " << move_to_uci(m) << " nodes " << search.nodes() << "\n";
  return m ? 0 : 1;
}
