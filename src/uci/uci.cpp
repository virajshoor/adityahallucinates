#include "uci/uci.hpp"
#include "movegen/movegen.hpp"
#include "board/bitboard.hpp"
#include "board/zobrist.hpp"
#include "movegen/magics.hpp"
#include "nnue/nnue.hpp"
#include "eval/eval.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <cstdlib>

namespace ah {

Move parse_move(const Position& pos, const std::string& token) {
  if (token.size() < 4) return MOVE_NONE;
  Square from = make_square(File(token[0] - 'a'), Rank(token[1] - '1'));
  Square to = make_square(File(token[2] - 'a'), Rank(token[3] - '1'));
  PieceType promo = KNIGHT;
  MoveType mt = NORMAL;
  if (token.size() >= 5) {
    mt = PROMOTION;
    switch (token[4]) {
      case 'q': promo = QUEEN; break;
      case 'r': promo = ROOK; break;
      case 'b': promo = BISHOP; break;
      case 'n': promo = KNIGHT; break;
      default: break;
    }
  }

  MoveListWrapper list(pos);
  for (const auto& em : list) {
    Move m = em.move;
    if (m.from() == from && m.to() == to) {
      if (m.type() == PROMOTION) {
        if (mt == PROMOTION && m.promotion_type() == promo) return m;
      } else {
        return m;
      }
    }
  }
  Move cand(from, to, mt, promo);
  if (pos.is_legal(cand)) return cand;
  Move castle(from, to, CASTLING);
  if (pos.is_legal(castle)) return castle;
  Move ep(from, to, EN_PASSANT);
  if (pos.is_legal(ep)) return ep;
  return MOVE_NONE;
}

void uci_loop() {
  Bitboards::init();
  Zobrist::init();
  Magics::init();

  // NNUE is opt-in via ADITYA_USE_NNUE=1 (classical remains stronger/faster by default).
  {
    const char* use = std::getenv("ADITYA_USE_NNUE");
    const char* env = std::getenv("ADITYA_NNUE");
    std::string path;
    if (env && env[0]) path = env;
    else if (use && use[0] == '1') path = "nets/fast.nnue";
    if (!path.empty()) {
      if (load_nnue(path))
        std::cerr << "info string loaded NNUE " << path << std::endl;
      else
        std::cerr << "info string failed NNUE " << path << std::endl;
    }
  }

  Position pos;
  StateInfo states[1024];
  int stateIdx = 0;
  pos.set_startpos(states[0]);

  Search search;
  search.set_hash(256);

  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream is(line);
    std::string token;
    is >> token;
    if (token.empty()) continue;

    if (token == "uci") {
      std::cout << "id name AdityaHallucinates\n";
      std::cout << "id author Viraj Shoor\n";
      std::cout << "option name Hash type spin default 256 min 1 max 65536\n";
      std::cout << "option name Threads type spin default 1 min 1 max 8\n";
      std::cout << "option name EvalFile type string default nets/default.nnue\n";
      std::cout << "uciok" << std::endl;
    } else if (token == "isready") {
      std::cout << "readyok" << std::endl;
    } else if (token == "ucinewgame") {
      search.clear();
      stateIdx = 0;
      pos.set_startpos(states[0]);
    } else if (token == "setoption") {
      std::string name, value, tmp;
      is >> tmp;
      is >> name;
      while (is >> tmp && tmp != "value") name += " " + tmp;
      if (!(is >> value)) value.clear();
      // read rest of line as value if needed
      std::string rest;
      std::getline(is, rest);
      if (!rest.empty()) value += rest;
      while (!value.empty() && value[0] == ' ') value.erase(0, 1);
      if (name == "Hash") search.set_hash(std::stoul(value));
      else if (name == "Threads") search.set_threads(std::stoi(value));
      else if (name == "EvalFile") {
        if (load_nnue(value))
          std::cout << "info string loaded NNUE " << value << std::endl;
        else
          std::cout << "info string failed to load NNUE " << value << std::endl;
      }
    } else if (token == "position") {
      is >> token;
      stateIdx = 0;
      if (token == "startpos") {
        pos.set_startpos(states[0]);
        is >> token;
      } else if (token == "fen") {
        std::string fen, part;
        while (is >> part && part != "moves") {
          if (!fen.empty()) fen += ' ';
          fen += part;
        }
        pos.set(fen, states[0]);
        token = part;
      }
      if (token == "moves") {
        while (is >> token) {
          Move m = parse_move(pos, token);
          if (!m) break;
          ++stateIdx;
          pos.do_move(m, states[stateIdx]);
        }
      }
    } else if (token == "go") {
      SearchLimits limits;
      while (is >> token) {
        if (token == "depth") is >> limits.depth;
        else if (token == "movetime") is >> limits.movetime;
        else if (token == "wtime") is >> limits.wtime;
        else if (token == "btime") is >> limits.btime;
        else if (token == "winc") is >> limits.winc;
        else if (token == "binc") is >> limits.binc;
        else if (token == "movestogo") is >> limits.movestogo;
        else if (token == "infinite") limits.infinite = true;
        else if (token == "nodes") { int n; is >> n; }
        else if (token == "searchmoves") {
          std::string sm;
          while (is >> sm) {
            Move m = parse_move(pos, sm);
            if (m) limits.searchmoves.push_back(m);
          }
        }
      }
      if (!limits.depth && !limits.movetime && !limits.wtime && !limits.btime && !limits.infinite)
        limits.depth = 6;

      Move best = search.think(pos, limits);
      std::cout << "bestmove " << move_to_uci(best) << std::endl;
    } else if (token == "stop") {
      search.request_stop();
    } else if (token == "quit") {
      search.request_stop();
      break;
    } else if (token == "d") {
      std::cout << pos.fen() << std::endl;
    } else if (token == "eval") {
      std::cout << "info string eval " << evaluate(pos)
                << (nnue_ready() ? " nnue" : " classical") << std::endl;
    }
  }
}

} // namespace ah
