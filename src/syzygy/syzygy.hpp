#pragma once

#include "board/board.hpp"

namespace ah {

// Initialize Syzygy from path (colon-separated). Empty disables.
bool syzygy_init(const char* path);
void syzygy_free();
int syzygy_max_pieces(); // TB_LARGEST, 0 if unavailable

// Probe WDL for side-to-move. Returns true on hit.
// wdl: -2 loss, -1 blessed loss, 0 draw, 1 cursed win, 2 win (TB codes remapped to SF-style).
bool syzygy_probe_wdl(const Position& pos, int& wdl);

} // namespace ah
