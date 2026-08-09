#include "nnue/nnue.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstdint>

namespace ah {

namespace {
NnueNet g_nnue;

static constexpr int PS_INPUT = 768;
static constexpr int HALFKA_INPUT = 64 * 10 * 64; // 40960

// Piece-square feature index (AHNNUEF1/2/3): fixed perspective, includes kings.
inline int feature_index_ps(Piece pc, Square sq, Color persp) {
  Square s = relative_square(persp, sq);
  PieceType pt = type_of(pc);
  Color rel = (color_of(pc) == persp) ? WHITE : BLACK;
  int p = (rel == WHITE ? 0 : 6) + (pt - PAWN);
  return p * 64 + int(s);
}

// HalfKA feature index (AHNNUEF4): king bucket × (own/enemy PNBRQ) × square.
// Returns -1 for kings (not encoded as piece features).
inline int feature_index_halfka(Piece pc, Square sq, Color persp, Square ksqAbs) {
  if (type_of(pc) == KING) return -1;
  Square ksq = relative_square(persp, ksqAbs);
  Square s = relative_square(persp, sq);
  PieceType pt = type_of(pc);
  Color rel = (color_of(pc) == persp) ? WHITE : BLACK;
  int p = (rel == WHITE ? 0 : 5) + (pt - PAWN); // 0-4 own, 5-9 enemy
  return (int(ksq) * 10 + p) * 64 + int(s);
}

struct NetWeights {
  int input = PS_INPUT, h1 = 128, h2 = 32;
  bool loaded = false;
  bool clipped = true;
  bool dual = false;   // concat [us|them] into FC1
  bool halfka = false; // AHNNUEF4 king-relative features
  std::vector<int16_t> w0; // feature-major: [feature][h1]
  std::vector<int16_t> b0;
  std::vector<int16_t> w1; // [h2][h1] or [h2][2*h1] if dual
  std::vector<int16_t> b1;
  std::vector<int16_t> w2;
  int32_t b2 = 0;
  float scale0 = 1.f, scale1 = 1.f, scale2 = 1.f;
  float inv_scale0 = 1.f;
};

static NetWeights g_w;

void quantize_from_float(const std::vector<float>& wf0, const std::vector<float>& bf0,
                         const std::vector<float>& wf1, const std::vector<float>& bf1,
                         const std::vector<float>& wf2, float bf2,
                         int input, int h1, int h2) {
  auto qrow = [](const std::vector<float>& src, std::vector<int16_t>& dst, float& scale) {
    float m = 1e-8f;
    for (float v : src) m = std::max(m, std::fabs(v));
    // Cap scale so int16 can represent the clipped activation domain used later.
    // Unbounded scale (tiny max-weight) saturates most weights to ±32767 and destroys HalfKA.
    scale = std::min(32767.f / m, 16384.f);
    dst.resize(src.size());
    for (size_t i = 0; i < src.size(); ++i)
      dst[i] = int16_t(std::clamp(int(std::lround(src[i] * scale)), -32767, 32767));
  };

  g_w.input = input;
  g_w.h1 = h1;
  g_w.h2 = h2;
  std::vector<float> w0t(size_t(input) * size_t(h1));
  for (int i = 0; i < h1; ++i)
    for (int f = 0; f < input; ++f)
      w0t[size_t(f) * size_t(h1) + size_t(i)] = wf0[size_t(i) * size_t(input) + size_t(f)];

  qrow(w0t, g_w.w0, g_w.scale0);
  g_w.inv_scale0 = 1.f / g_w.scale0;
  g_w.b0.resize(h1);
  for (int i = 0; i < h1; ++i)
    g_w.b0[i] = int16_t(std::clamp(int(std::lround(bf0[i] * g_w.scale0)), -32767, 32767));

  qrow(wf1, g_w.w1, g_w.scale1);
  g_w.b1.resize(h2);
  for (int j = 0; j < h2; ++j)
    g_w.b1[j] = int16_t(std::clamp(int(std::lround(bf1[j] * g_w.scale1)), -32767, 32767));

  qrow(wf2, g_w.w2, g_w.scale2);
  g_w.b2 = int32_t(std::lround(bf2));
  g_w.loaded = true;
  g_nnue.loaded = true;
}

inline void add_feature(NnueAccumulator& a, Color persp, int f) {
  if (f < 0) return;
  const int h1 = g_w.h1;
  const int16_t* col = &g_w.w0[size_t(f) * size_t(h1)];
  int16_t* acc = a.acc[persp];
  for (int i = 0; i < h1; ++i) acc[i] = int16_t(acc[i] + col[i]);
}

inline void sub_feature(NnueAccumulator& a, Color persp, int f) {
  if (f < 0) return;
  const int h1 = g_w.h1;
  const int16_t* col = &g_w.w0[size_t(f) * size_t(h1)];
  int16_t* acc = a.acc[persp];
  for (int i = 0; i < h1; ++i) acc[i] = int16_t(acc[i] - col[i]);
}

inline int feature_for(Piece pc, Square sq, Color persp, const NnueAccumulator& a) {
  if (g_w.halfka)
    return feature_index_halfka(pc, sq, persp, a.ksq[persp]);
  return feature_index_ps(pc, sq, persp);
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
  bool dual = false;
  bool halfka = false;
  if (std::memcmp(magic, "AHNNUEF1", 8) == 0) clipped = false;
  else if (std::memcmp(magic, "AHNNUEF2", 8) == 0) clipped = true;
  else if (std::memcmp(magic, "AHNNUEF3", 8) == 0) { clipped = true; dual = true; }
  else if (std::memcmp(magic, "AHNNUEF4", 8) == 0) { clipped = true; dual = true; halfka = true; }
  else return false;

  int32_t dims[3] = {};
  in.read(reinterpret_cast<char*>(dims), sizeof(dims));
  if (dims[2] != 32) return false;
  if (dims[1] != 256 && dims[1] != 128) return false;
  if (halfka) {
    if (dims[0] != HALFKA_INPUT) return false;
  } else {
    if (dims[0] != PS_INPUT) return false;
  }

  const int h1 = dims[1];
  const int h2 = dims[2];
  const int fc1_in = dual ? h1 * 2 : h1;

  std::vector<float> w0(size_t(h1) * size_t(dims[0])), b0(h1), w1(size_t(h2) * size_t(fc1_in)), b1(h2), w2(h2);
  float b2 = 0.f;
  in.read(reinterpret_cast<char*>(w0.data()), w0.size() * sizeof(float));
  in.read(reinterpret_cast<char*>(b0.data()), b0.size() * sizeof(float));
  in.read(reinterpret_cast<char*>(w1.data()), w1.size() * sizeof(float));
  in.read(reinterpret_cast<char*>(b1.data()), b1.size() * sizeof(float));
  in.read(reinterpret_cast<char*>(w2.data()), w2.size() * sizeof(float));
  in.read(reinterpret_cast<char*>(&b2), sizeof(float));
  if (!in) return false;
  quantize_from_float(w0, b0, w1, b1, w2, b2, dims[0], h1, h2);
  g_w.clipped = clipped;
  g_w.dual = dual;
  g_w.halfka = halfka;
  std::cerr << "info string nnue_flags dual=" << dual << " halfka=" << halfka
            << " input=" << dims[0] << " h1=" << h1 << std::endl;
  return true;
}

void NnueNet::refresh(NnueAccumulator& a, const Position& pos) const {
  if (!g_w.loaded) { a.computed = false; return; }
  const int h1 = g_w.h1;
  a.ksq[WHITE] = pos.king_square(WHITE);
  a.ksq[BLACK] = pos.king_square(BLACK);
  for (Color persp : {WHITE, BLACK}) {
    for (int i = 0; i < h1; ++i) a.acc[persp][i] = g_w.b0[i];
    Bitboard bb = pos.pieces();
    while (bb) {
      Square sq = pop_lsb(bb);
      Piece pc = pos.piece_on(sq);
      if (g_w.halfka && type_of(pc) == KING) continue;
      add_feature(a, persp, feature_for(pc, sq, persp, a));
    }
  }
  a.computed = true;
}

void NnueNet::put_piece(NnueAccumulator& a, Piece pc, Square sq) const {
  if (!g_w.loaded || !a.computed) return;
  // King move changes every HalfKA bucket — force full refresh later.
  if (g_w.halfka && type_of(pc) == KING) {
    a.computed = false;
    return;
  }
  add_feature(a, WHITE, feature_for(pc, sq, WHITE, a));
  add_feature(a, BLACK, feature_for(pc, sq, BLACK, a));
}

void NnueNet::remove_piece(NnueAccumulator& a, Piece pc, Square sq) const {
  if (!g_w.loaded || !a.computed) return;
  if (g_w.halfka && type_of(pc) == KING) {
    a.computed = false;
    return;
  }
  sub_feature(a, WHITE, feature_for(pc, sq, WHITE, a));
  sub_feature(a, BLACK, feature_for(pc, sq, BLACK, a));
}

Value NnueNet::evaluate_acc(const NnueAccumulator& a, Color stm) const {
  if (!g_w.loaded || !a.computed) return VALUE_NONE;
  const auto& n = g_w;

  // Prefer float path for HalfKA (int16 QA still lossy on large embedding tables).
  bool use_float = g_w.halfka;
  if (const char* e = std::getenv("ADITYA_NNUE_FLOAT")) {
    if (e[0] == '1') use_float = true;
    if (e[0] == '0') use_float = false;
  }
  if (use_float) {
    const float inv0 = n.inv_scale0;
    auto crelu1 = [&](const int16_t* acc, float* out) {
      for (int i = 0; i < n.h1; ++i) {
        float v = float(acc[i]) * inv0;
        out[i] = n.clipped ? (v < 0.f ? 0.f : (v > 1.f ? 1.f : v)) : (v > 0.f ? v : 0.f);
      }
    };
    float h1a[256], h1b[256];
    crelu1(a.acc[stm], h1a);
    const int fc1_in = n.dual ? n.h1 * 2 : n.h1;
    if (n.dual) crelu1(a.acc[~stm], h1b);
    float h2v[32];
    const float inv1 = 1.f / n.scale1;
    for (int j = 0; j < n.h2; ++j) {
      float sum = float(n.b1[j]) * inv1;
      const int16_t* row = &n.w1[size_t(j) * size_t(fc1_in)];
      for (int i = 0; i < n.h1; ++i) sum += float(row[i]) * inv1 * h1a[i];
      if (n.dual)
        for (int i = 0; i < n.h1; ++i) sum += float(row[n.h1 + i]) * inv1 * h1b[i];
      h2v[j] = n.clipped ? (sum < 0.f ? 0.f : (sum > 1.f ? 1.f : sum)) : (sum > 0.f ? sum : 0.f);
    }
    float out = float(n.b2);
    const float inv2 = 1.f / n.scale2;
    for (int j = 0; j < n.h2; ++j) out += float(n.w2[j]) * inv2 * h2v[j];
    int cp = int(std::lround(out));
    return Value(std::clamp(cp, -VALUE_MATE_IN_MAX_PLY + 1, VALUE_MATE_IN_MAX_PLY - 1));
  }

  // Integer CReLU path. Activations kept in int32 — scale0 may exceed int16 range.
  const int32_t s0 = std::max(1, int32_t(std::lround(n.scale0)));
  const int32_t s1 = std::max(1, int32_t(std::lround(n.scale1)));
  const int64_t den1 = int64_t(s0) * int64_t(s1);

  int32_t h1a[256];
  int32_t h1b[256];
  auto crelu_i32 = [&](const int16_t* acc, int32_t* out) {
    for (int i = 0; i < n.h1; ++i) {
      int32_t v = int32_t(acc[i]);
      if (n.clipped) {
        if (v < 0) v = 0;
        else if (v > s0) v = s0;
      } else if (v < 0) {
        v = 0;
      }
      out[i] = v;
    }
  };
  crelu_i32(a.acc[stm], h1a);
  const int fc1_in = n.dual ? n.h1 * 2 : n.h1;
  if (n.dual) crelu_i32(a.acc[~stm], h1b);

  int32_t h2v[32];
  for (int j = 0; j < n.h2; ++j) {
    int64_t sum = int64_t(n.b1[j]) * s0;
    const int16_t* row = &n.w1[size_t(j) * size_t(fc1_in)];
    for (int i = 0; i < n.h1; ++i) sum += int64_t(row[i]) * int64_t(h1a[i]);
    if (n.dual)
      for (int i = 0; i < n.h1; ++i) sum += int64_t(row[n.h1 + i]) * int64_t(h1b[i]);
    if (n.clipped) {
      if (sum < 0) sum = 0;
      else if (sum > den1) sum = den1;
    } else if (sum < 0) {
      sum = 0;
    }
    h2v[j] = int32_t(sum);
  }

  const int32_t s2 = std::max(1, int32_t(std::lround(n.scale2)));
  int64_t accOut = int64_t(n.b2) * int64_t(s2) * den1;
  for (int j = 0; j < n.h2; ++j) accOut += int64_t(n.w2[j]) * int64_t(h2v[j]);
  const int64_t denOut = int64_t(s2) * den1;
  int cp = int((accOut + (accOut >= 0 ? denOut / 2 : -(denOut / 2))) / denOut);
  return Value(std::clamp(cp, -VALUE_MATE_IN_MAX_PLY + 1, VALUE_MATE_IN_MAX_PLY - 1));
}

Value NnueNet::evaluate(const Position& pos) const {
  if (!g_w.loaded) return VALUE_NONE;
  NnueAccumulator a;
  refresh(a, pos);
  return evaluate_acc(a, pos.side_to_move());
}

Value NnueNet::evaluate(const Position& pos, const NnueAccumulator& a) const {
  if (!g_w.loaded || !a.computed) return evaluate(pos);
  return evaluate_acc(a, pos.side_to_move());
}

void NnueNet::do_move(NnueAccumulator& child, const NnueAccumulator& parent,
                      const Position& pos, Move m) const {
  if (!g_w.loaded || !parent.computed) {
    child.computed = false;
    return;
  }
  child.copy_from(parent);
  Color us = pos.side_to_move();
  Square from = m.from(), to = m.to();
  Piece pc = pos.piece_on(from);

  // King moves (incl. castling) invalidate HalfKA buckets.
  if (g_w.halfka && (type_of(pc) == KING || m.type() == CASTLING)) {
    child.computed = false;
    return;
  }

  if (m.type() == CASTLING) {
    bool kingSide = to > from;
    Square kto = to;
    Square rto = make_square(kingSide ? FILE_F : FILE_D, rank_of(from));
    Square rookFrom = make_square(kingSide ? FILE_H : FILE_A, rank_of(from));
    Piece rook = make_piece(us, ROOK);
    remove_piece(child, pc, from);
    remove_piece(child, rook, rookFrom);
    put_piece(child, pc, kto);
    put_piece(child, rook, rto);
    return;
  }

  Piece captured = m.type() == EN_PASSANT ? make_piece(~us, PAWN) : pos.piece_on(to);
  if (captured) {
    Square capsq = to;
    if (m.type() == EN_PASSANT) capsq = Square(to - pawn_push(us));
    remove_piece(child, captured, capsq);
  }
  remove_piece(child, pc, from);
  if (m.type() == PROMOTION) {
    put_piece(child, make_piece(us, m.promotion_type()), to);
  } else {
    put_piece(child, pc, to);
  }
}

bool NnueNet::load(const std::string& path) { return load_nnue(path); }

} // namespace ah
