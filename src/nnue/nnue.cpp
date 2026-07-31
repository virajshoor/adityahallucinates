#include "nnue/nnue.hpp"
#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

namespace ah {

namespace {
NnueNet g_nnue;

inline float crelu(float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }

inline int feature_index(Piece pc, Square sq, Color stm) {
  Square s = relative_square(stm, sq);
  PieceType pt = type_of(pc);
  Color rel = (color_of(pc) == stm) ? WHITE : BLACK;
  int p = (rel == WHITE ? 0 : 6) + (pt - PAWN);
  return p * 64 + int(s);
}

struct NnueNetFloat {
  int input = 768, h1 = 256, h2 = 32;
  bool loaded = false;
  bool clipped = false;
  std::vector<float> w0, b0, w1, b1, w2;
  float b2 = 0.f;
};

static NnueNetFloat g_float;
} // namespace

NnueNet& nnue() { return g_nnue; }
bool nnue_ready() { return g_float.loaded; }

bool load_nnue(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  char magic[8] = {};
  in.read(magic, 8);
  bool clipped = false;
  if (std::memcmp(magic, "AHNNUEF1", 8) == 0) {
    clipped = false;
  } else if (std::memcmp(magic, "AHNNUEF2", 8) == 0) {
    clipped = true;
  } else {
    return false;
  }
  int32_t dims[3] = {};
  in.read(reinterpret_cast<char*>(dims), sizeof(dims));
  if (dims[0] != 768 || dims[2] != 32) return false;
  if (dims[1] != 256 && dims[1] != 128) return false;

  auto& n = g_float;
  n.input = dims[0];
  n.h1 = dims[1];
  n.h2 = dims[2];
  n.clipped = clipped;
  n.w0.resize(n.h1 * n.input);
  n.b0.resize(n.h1);
  n.w1.resize(n.h2 * n.h1);
  n.b1.resize(n.h2);
  n.w2.resize(n.h2);
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

  // Stack buffers sized for max H1=256
  float acc[256];
  std::memcpy(acc, n.b0.data(), sizeof(float) * n.h1);

  Bitboard bb = pos.pieces();
  while (bb) {
    Square sq = pop_lsb(bb);
    int f = feature_index(pos.piece_on(sq), sq, stm);
    const float* col = &n.w0[0] + f; // will index as [i * input + f]
    for (int i = 0; i < n.h1; ++i)
      acc[i] += n.w0[i * n.input + f];
    (void)col;
  }

  float h1a[256];
  if (n.clipped) {
    for (int i = 0; i < n.h1; ++i) h1a[i] = crelu(acc[i]);
  } else {
    for (int i = 0; i < n.h1; ++i) h1a[i] = acc[i] > 0.f ? acc[i] : 0.f;
  }

  float h2v[32];
  for (int j = 0; j < n.h2; ++j) {
    float sum = n.b1[j];
    const float* row = &n.w1[j * n.h1];
    for (int i = 0; i < n.h1; ++i) sum += row[i] * h1a[i];
    h2v[j] = n.clipped ? crelu(sum) : (sum > 0.f ? sum : 0.f);
  }
  float out = n.b2;
  for (int j = 0; j < n.h2; ++j) out += n.w2[j] * h2v[j];
  int cp = int(std::lround(out));
  return Value(std::clamp(cp, -VALUE_MATE_IN_MAX_PLY + 1, VALUE_MATE_IN_MAX_PLY - 1));
}

bool NnueNet::load(const std::string& path) { return load_nnue(path); }

} // namespace ah
