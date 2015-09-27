#ifndef SPLAY_VIS_H
#define SPLAY_VIS_H

#include "gui.h"
#include <vector>

namespace splay {
double draw_spetrum_data(bitmap_window& bw, const std::vector<short>& data, int sample_rate);
void draw_waveform_data(bitmap_window& bw, const std::vector<short>& data);
} // namespace splay
#endif