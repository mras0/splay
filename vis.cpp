#include "vis.h"
#include "constants.h"
#include <assert.h>
#include <complex>
#include <vector>
#include <algorithm>

namespace splay {

void draw_line(unsigned* pixels, int w, int h, int x0, int y0, int x1, int y1, unsigned color)
{
    assert(x0 >= 0 && x0 < w);
    assert(y0 >= 0 && y0 < h);
    assert(x1 >= 0 && x1 < w);
    assert(y1 >= 0 && y1 < h);
    (void)h;

    auto pp = [pixels, w, color](int x, int y) { pixels[x+y*w] = color; };

    if (x0 == x1 && y0 == y1) {
        pp(x0, y0);
        return;
    }

    if (abs(x0-x1) > abs(y0-y1)) {
        if (x0 > x1) {
            std::swap(x0, x1);
            std::swap(y0, y1);
        }
        float y = (float)y0;
        const float incr = float(y1-y0) / (x1-x0);
        for (int x = x0; x <= x1; ++x) {
            pp(x, (int)y);
            y += incr;
        }
    } else {
        if (y0 > y1) {
            std::swap(x0, x1);
            std::swap(y0, y1);
        }
        float x = (float)x0;
        const float incr = float(x1-x0) / (y1-y0);
        for (int y = y0; y <= y1; ++y) {
            pp((int)x, y);
            x += incr;
        }
    }
}

/* Based on http://paulbourke.net/miscellaneous/dft/fft_ms.c
This computes an in-place complex-to-complex FFT
x and y are the real and imaginary arrays of 2^m points.
dir =  1 givesu forward transform
dir = -1 gives reverse transform
*/
void fft(std::vector<std::complex<float>>& x, int m, int dir = 1)
{
    int i, i1, i2, j, k, l, l1, l2, n;
    std::complex<float> tx, t1, u, c;

    /*Calculate the number of points */
    n = 1;
    for (i = 0; i < m; i++)
        n <<= 1;

    /* Do the bit reversal */
    i2 = n >> 1;
    j = 0;

    for (i = 0; i < n-1; i++) {
        if (i < j)
            swap(x[i], x[j]);

        k = i2;

        while (k <= j) {
            j -= k;
            k >>= 1;
        }

        j += k;
    }

    /* Compute the FFT */
    c.real(-1.0);
    c.imag(0.0);
    l2 = 1;
    for (l = 0; l < m; l++) {
        l1 = l2;
        l2 <<= 1;
        u.real(1.0);
        u.imag(0.0);

        for (j = 0; j < l1; j++) {
            for (i = j; i < n; i += l2) {
                i1 = i + l1;
                t1 = u * x[i1];
                x[i1] = x[i] - t1;
                x[i] += t1;
            }

            u = u * c;
        }

        c.imag(sqrt((1.0f - c.real()) / 2.0f));
        if (dir == 1)
            c.imag(-c.imag());
        c.real(sqrt((1.0f + c.real()) / 2.0f));
    }

    /* Scaling for forward transform */
    if (dir == 1) {
        for (i = 0; i < n; i++)
            x[i] /= static_cast<float>(n);
    }
}

void best_fit_resample(std::vector<float>& x, unsigned max_size)
{
    assert(max_size > 0);
    while (x.size() > max_size) {
        for (size_t i = 0, s = x.size() / 2; i < s; ++i) {
            x[i] = 0.5f * (x[i*2+0] + x[i*2+1]);
        }
        x.resize(x.size() / 2);
    }
}

double do_draw_spetrum_data(bitmap_window& bw, std::vector<std::complex<float>>& f, std::vector<float>& spectrum, int m)
{
    const int w = bw.width();
    const int h = bw.height();

    assert(f.size() == (1ULL<<m));
    fft(f, m);
    f.resize(f.size() / 2); // Ignore negative frequencies

    spectrum.resize(f.size());
    std::transform(f.begin(), f.end(), spectrum.begin(), [](std::complex<float> c) { return abs(c); });

    // Determine max frequency
    const auto index = std::max_element(spectrum.begin() + 1, spectrum.end()) - spectrum.begin(); // +1 because we don't care about DC
    const auto max_freq = double(index) * 2 * samplerate / (1<<m);

    // Resample to fit
    best_fit_resample(spectrum, w);

    std::vector<unsigned> pixels(w * h, ~0U);
    for (int i = 0; i < w; ++i) {
        auto samp = spectrum[i * spectrum.size() / w];
        assert(samp >= 0.0 && samp <= 1.0);

        samp *= 20;
        if (samp < 0) samp = 0;
        if (samp > 1) samp = 1;

        int y = int(samp * (h-1));

        draw_line(&pixels[0], w, h, i, 0, i, y, 0);
    }
    bw.update_pixels(&pixels[0]);

    return max_freq;
}


void draw_waveform_data(bitmap_window& bw, const std::vector<short>& data)
{
    const int w = bw.width();
    const int h = bw.height();
    std::vector<unsigned> pixels(w * h, ~0U);
    int lx = 0, ly = h/2;
    for (int i = 0; i < w; ++i) {
        const auto samp = data[i * data.size() / w] / 32767.0;
        assert(samp >= -1.0 && samp <= 1.0);

        int y = int(samp * (h/2-1) + (h/2));

        draw_line(&pixels[0], w, h, lx, ly, i, y, 0);
        lx = i;
        ly = y;
    }
    bw.update_pixels(&pixels[0]);
}

class spectrum_analyzer::impl {
public:
    impl() {}

    double draw_spetrum_data(bitmap_window& bw, const std::vector<short>& data) {
        temp_.resize(data.size());
        std::transform(begin(data), end(data), temp_.begin(), [](short s) { return s * (1.0f/32767.0f); });
        // Make pow2
        int m = 0;
        for (unsigned i = 1; ; i<<=1, m++) {
            if (temp_.size() <= i) {
                temp_.resize(i);
                break;
            }
        }
        return do_draw_spetrum_data(bw, temp_, spectrum_, m);
    }

private:
    std::vector<std::complex<float>> temp_;
    std::vector<float>               spectrum_;
};


spectrum_analyzer::spectrum_analyzer() : impl_(new impl())
{
}

spectrum_analyzer::~spectrum_analyzer() = default;

double spectrum_analyzer::draw_spetrum_data(bitmap_window& bw, const std::vector<short>& data)
{
    return impl_->draw_spetrum_data(bw, data);
}


} // namespace splay