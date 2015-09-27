#ifndef SPLAY_VIS_H
#define SPLAY_VIS_H

#include "gui.h"
#include <vector>
#include <memory>

namespace splay {

class spectrum_analyzer {
public:
    spectrum_analyzer();
    ~spectrum_analyzer();

    double draw_spetrum_data(bitmap_window& bw, const std::vector<short>& data);
private:
    class impl;
    std::unique_ptr<impl> impl_;
};

void draw_waveform_data(bitmap_window& bw, const std::vector<short>& data);
} // namespace splay
#endif