#include "nnue/nnue.hpp"
#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstdint>

namespace ah {

namespace {
NnueNet g_nnue;

inline int feature_index(Piece pc, Square sq, Color stm) {
  Square s = relative_square(stm, sq);
  PieceType pt = type_of(pc);
  Color rel = (color_of(pc) == stm) ? WHITE : BLACK;
  int p = (rel == WHITE ? 0 : 6) + (pt - PAWN);
  return p * 64 + int(s);
}

struct NetWeights {
  int input = 768, h1 = 128, h2 = 32;
  bool loaded = false;
  bool clipped = true;
  // Feature transformer stored as [feature][h1] for incremental/refresh adds
  std::vector<int16_t> w0; // input * h1
  std::vector<int16_t> b0; // h1
  std::vector<int16_t> w1; // h2 * h1
  std::vector<int16_t> b1; // h2
  std::vector<int16_t> w2; // h2
  int32_t b2 = 0;
  float scale0 = 1.f, scale1 = 1.f, scale2 = 1.f; // multiply quantized -> float space
  // Final: ((acc*s0 -> crelu) * w1*s1 -> crelu) * w2*s2 + b2  in cp
};

static NetWeights g_w;

void quantize_from_float(const std::vector<float>& wf0, const std::vector<float>& bf0,
                         const std::vector<float>& wf1, const std::vector<float>& bf1,
                         const std::vector<float>& wf2, float bf2,
                         int input, int h1, int h2) {
  auto qrow = [](const std::vector<float>& src, std::vector<int16_t>& dst, float& scale) {
    float m = 1e-8f;
    for (float v : src) m = std::max(m, std::fabs(v));
    scale = 32767.f / m;
    dst.resize(src.size());
    for (size_t i = 0; i < src.size(); ++i)
      dst[i] = int16_t(std::clamp(int(std::lround(src[i] * scale)), -32767, 32767));
  };

  g_w.input = input;
  g_w.h1 = h1;
  g_w.h2 = h2;
  // Transpose w0 to feature-major: for each feature f, h1 weights
  // Incoming wf0 is row-major H1 x INPUT (as exported)
  std::vector<float> w0t(input * h1);
  for (int i = 0; i < h1; ++i)
    for (int f = 0; f < input; ++f)
      w0t[f * h1 + i] = wf0[i * input + f];

  qrow(w0t, g_w.w0, g_w.scale0);
  // Bias in same scale as w0*feature(1)
  {
    float m = 1e-8f;
    for (float v : bf0) m = std::max(m, std::fabs(v));
    // Use same scale0 for bias compatibility with accumulator
    g_w.b0.resize(h1);
    for (int i = 0; i < h1; ++i)
      g_w.b0[i] = int16_t(std::clamp(int(std::lround(bf0[i] * g_w.scale0)), -32767, 32767));
  }
  qrow(wf1, g_w.w1, g_w.scale1);
  {
    g_w.b1.resize(h2);
    for (int j = 0; j < h2; ++j)
      g_w.b1[j] = int16_t(std::clamp(int(std::lround(bf1[j] * g_w.scale1)), -32767, 32767));
  }
  // Output already in cp for F2 nets
  qrow(wf2, g_w.w2, g_w.scale2);
  g_w.b2 = int32_t(std::lround(bf2)); // already cp
  g_w.loaded = true;
  g_nnue.loaded = true;
}
} // namespace

NnueNet& nnue() { return g_nnue; }
bool nnue_ready() { return g_w.loaded; }

bool load_nnue(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  char magic[8] = {};
  in.read(magic, 8);
  bool clipped = true;
  if (std::memcmp(magic, "AHNNUEF1", 8) == 0) clipped = false;
  else if (std::memcmp(magic, "AHNNUEF2", 8) == 0) clipped = true;
  else return false;

  int32_t dims[3] = {};
  in.read(reinterpret_cast<char*>(dims), sizeof(dims));
  if (dims[0] != 768 || dims[2] != 32) return false;
  if (dims[1] != 256 && dims[1] != 128) return false;

  std::vector<float> w0(dims[1] * dims[0]), b0(dims[1]), w1(dims[2] * dims[1]), b1(dims[2]), w2(dims[2]);
  float b2 = 0.f;
  in.read(reinterpret_cast<char*>(w0.data()), w0.size() * sizeof(float));
  in.read(reinterpret_cast<char*>(b0.data()), b0.size() * sizeof(float));
  in.read(reinterpret_cast<char*>(w1.data()), w1.size() * sizeof(float));
  in.read(reinterpret_cast<char*>(b1.data()), b1.size() * sizeof(float));
  in.read(reinterpret_cast<char*>(w2.data()), w2.size() * sizeof(float));
  in.read(reinterpret_cast<char*>(&b2), sizeof(float));
  if (!clipped) {
    // Old F1 nets output cp already in training; keep as-is
  }
  quantize_from_float(w0, b0, w1, b1, w2, b2, dims[0], dims[1], dims[2]);
  g_w.clipped = clipped;
  return true;
}

Value NnueNet::evaluate(const Position& pos) const {
  if (!g_w.loaded) return VALUE_NONE;
  const auto& n = g_w;
  Color stm = pos.side_to_move();

  // Accumulator in quantized domain of layer0
  int32_t acc[256];
  for (int i = 0; i < n.h1; ++i) acc[i] = n.b0[i];

  Bitboard bb = pos.pieces();
  while (bb) {
    Square sq = pop_lsb(bb);
    int f = feature_index(pos.piece_on(sq), sq, stm);
    const int16_t* col = &n.w0[f * n.h1];
    for (int i = 0; i < n.h1; ++i) acc[i] += col[i];
  }

  // Hidden1: dequant to float, clipped ReLU to [0,1]
  float h1a[256];
  const float inv0 = 1.f / n.scale0;
  for (int i = 0; i < n.h1; ++i) {
    float v = float(acc[i]) * inv0;
    if (n.clipped) h1a[i] = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
    else h1a[i] = v > 0.f ? v : 0.f;
  }

  // Layer1: accumulate in float then clamp
  float h2v[32];
  const float inv1 = 1.f / n.scale1;
  for (int j = 0; j < n.h2; ++j) {
    float sum = float(n.b1[j]) * inv1;
    const int16_t* row = &n.w1[j * n.h1];
    for (int i = 0; i < n.h1; ++i) sum += float(row[i]) * inv1 * h1a[i];
    h2v[j] = n.clipped ? (sum < 0.f ? 0.f : (sum > 1.f ? 1.f : sum)) : (sum > 0.f ? sum : 0.f);
  }

  float out = float(n.b2);
  const float inv2 = 1.f / n.scale2;
  for (int j = 0; j < n.h2; ++j) out += float(n.w2[j]) * inv2 * h2v[j];
  int cp = int(std::lround(out));
  return Value(std::clamp(cp, -VALUE_MATE_IN_MAX_PLY + 1, VALUE_MATE_IN_MAX_PLY - 1));
}

bool NnueNet::load(const std::string& path) { return load_nnue(path); }

} // namespace ah
