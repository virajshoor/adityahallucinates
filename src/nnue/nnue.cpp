#include "nnue/nnue.hpp"
#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

namespace ah {

namespace {
NnueNet g_nnue;

inline float relu(float x) { return x > 0.f ? x : 0.f; }

inline int feature_index(Piece pc, Square sq, Color stm) {
  Square s = relative_square(stm, sq);
  PieceType pt = type_of(pc);
  Color rel = (color_of(pc) == stm) ? WHITE : BLACK;
  int p = (rel == WHITE ? 0 : 6) + (pt - PAWN);
  return p * 64 + int(s);
}
} // namespace

// Keep float storage for correctness of bootstrap nets
struct NnueNetFloat {
  static constexpr int INPUT = 768, H1 = 256, H2 = 32;
  bool loaded = false;
  std::vector<float> w0, b0, w1, b1, w2;
  float b2 = 0.f;
};

static NnueNetFloat g_float;

NnueNet& nnue() { return g_nnue; }
bool nnue_ready() { return g_float.loaded; }

bool load_nnue(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  char magic[8] = {};
  in.read(magic, 8);
  if (std::memcmp(magic, "AHNNUEF1", 8) != 0) return false;
  int32_t dims[3] = {};
  in.read(reinterpret_cast<char*>(dims), sizeof(dims));
  if (dims[0] != NnueNetFloat::INPUT || dims[1] != NnueNetFloat::H1 || dims[2] != NnueNetFloat::H2)
    return false;
  auto& n = g_float;
  n.w0.resize(NnueNetFloat::H1 * NnueNetFloat::INPUT);
  n.b0.resize(NnueNetFloat::H1);
  n.w1.resize(NnueNetFloat::H2 * NnueNetFloat::H1);
  n.b1.resize(NnueNetFloat::H2);
  n.w2.resize(NnueNetFloat::H2);
  in.read(reinterpret_cast<char*>(n.w0.data()), n.w0.size() * sizeof(float));
  in.read(reinterpret_cast<char*>(n.b0.data()), n.b0.size() * sizeof(float));
  in.read(reinterpret_cast<char*>(n.w1.data()), n.w1.size() * sizeof(float));
  in.read(reinterpret_cast<char*>(n.b1.data()), n.b1.size() * sizeof(float));
  in.read(reinterpret_cast<char*>(n.w2.data()), n.w2.size() * sizeof(float));
  in.read(reinterpret_cast<char*>(&n.b2), sizeof(float));
  n.loaded = true;
  g_nnue.loaded = true;
  return true;
}

Value NnueNet::evaluate(const Position& pos) const {
  if (!g_float.loaded) return VALUE_NONE;
  const auto& n = g_float;
  Color stm = pos.side_to_move();
  float acc[NnueNetFloat::H1];
  std::memcpy(acc, n.b0.data(), sizeof(float) * NnueNetFloat::H1);
  Bitboard bb = pos.pieces();
  while (bb) {
    Square sq = pop_lsb(bb);
    int f = feature_index(pos.piece_on(sq), sq, stm);
    for (int i = 0; i < NnueNetFloat::H1; ++i)
      acc[i] += n.w0[i * NnueNetFloat::INPUT + f];
  }
  float h1a[NnueNetFloat::H1];
  for (int i = 0; i < NnueNetFloat::H1; ++i) h1a[i] = relu(acc[i]);
  float h2v[NnueNetFloat::H2];
  for (int j = 0; j < NnueNetFloat::H2; ++j) {
    float sum = n.b1[j];
    const float* row = &n.w1[j * NnueNetFloat::H1];
    for (int i = 0; i < NnueNetFloat::H1; ++i) sum += row[i] * h1a[i];
    h2v[j] = relu(sum);
  }
  float out = n.b2;
  for (int j = 0; j < NnueNetFloat::H2; ++j) out += n.w2[j] * h2v[j];
  int cp = int(std::lround(out));
  return Value(std::clamp(cp, -VALUE_MATE_IN_MAX_PLY + 1, VALUE_MATE_IN_MAX_PLY - 1));
}

bool NnueNet::load(const std::string& path) { return load_nnue(path); }

} // namespace ah
