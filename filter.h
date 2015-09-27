#ifndef SPLASY_FILTER_H
#define SPLASY_FILTER_H

#include <cassert>
#include <cmath>
#include "constants.h"

namespace splay {

enum class filter_type { lowpass, bandpass, highpass };
constexpr int filter_type_count = static_cast<int>(filter_type::highpass) + 1;
static const char* filter_type_names[filter_type_count] ={"lowpass", "bandpass", "highpass"};

class simple_lowpass_filter {
public:
    simple_lowpass_filter() : alpha_(0.5f), last_(0) {
    }

    float operator()(const float in) {
        const float out = last_ + alpha_ * (in - last_);
        last_ = out;
        return out;
    }

    void cutoff_frequeny(float freq) {
        const float x = 2.0f * pi * freq / samplerate;
        alpha(x / (x+1));
    }

    void alpha(float alpha) {
        assert(alpha >= 0.0f && alpha <= 1.0f);
        alpha_ = alpha;
    }

private:
    float alpha_;
    float last_;
};

class biquad_filter {
public:
    biquad_filter() {
    }

    void filter(filter_type ft) {
        type_ = ft;
        cutoff_frequeny(freq_);
    }

    void cutoff_frequeny(float freq) {
        // http://basicsynth.com/index.php?page=filters&WEBMGR=...3bf4c0509ba3e1b29dc79b64a
        // Lowpass
        freq_ = freq;
        switch (type_) {
        case filter_type::lowpass:
            {
                const float c = 1 / tan((pi / samplerate) * freq);
                const float c2 = c * c;
                const float csqr2 = sqrt2 * c;
                const float d = (c2 + csqr2 + 1);

                amp_in_0 = 1 / d;
                amp_in_1 = amp_in_0 + amp_in_0;
                amp_in_2 = amp_in_0;
                amp_out_1 = (2 * (1 - c2)) / d;
                amp_out_2 = (c2 - csqr2 + 1) / d;
            }
            break;
        case filter_type::bandpass:
            {
                const float c = 1 / tan((pi / samplerate) * freq);
                const float d = 1 + c;
                amp_in_0 = 1 / d;
                amp_in_1 = 0;
                amp_in_2 = -amp_in_0;
                amp_out_1 = (-c*2*cos(2*pi*freq/samplerate)) / d;
                amp_out_2 = (c - 1) / d;
            }
            break;
        case filter_type::highpass:
            {
                const float c = tan((pi / samplerate) * freq);
                const float c2 = c * c;
                const float csqr2 = sqrt2 * c;
                const float d = (c2 + csqr2 + 1);

                amp_in_0 = 1 / d;
                amp_in_1 = -(amp_in_0 + amp_in_0);
                amp_in_2 = amp_in_0;
                amp_out_1 = (2 * (c2 - 1)) / d;
                amp_out_2 = (1 - csqr2 + c2) / d;
            }
            break;
        }
    }

    float operator()(float in) {
        const float out = (amp_in_0 * in)
                        + (amp_in_1 * old_in_1)
                        + (amp_in_2 * old_in_2)
                        - (amp_out_1 * old_out_1)
                        - (amp_out_2 * old_out_2);
        old_out_2 = old_out_1;
        old_out_1 = out;
        old_in_2 = old_in_1;
        old_in_1 = in;
        return 0.5f*out;
    }

private:
    filter_type type_ = filter_type::lowpass;
    float freq_ = 0;
    float old_in_1   = 0.0f;
    float old_in_2   = 0.0f;
    float old_out_1  = 0.0f;
    float old_out_2  = 0.0f;
    float amp_in_0   = 0.0f;
    float amp_in_1   = 0.0f;
    float amp_in_2   = 0.0f;
    float amp_out_1  = 0.0f;
    float amp_out_2  = 0.0f;
};

// http://www.musicdsp.org/showone.php?id=29
class filter_2lp_in_series {
public:
    explicit filter_2lp_in_series() {}

    void filter(filter_type ft) {
        type_ = ft;
    }

    void cutoff_frequeny(float freq) {
        freq_ = freq;
        update_coefficients();
    }

    void resonance(float resonance) {
        resonance_ = resonance;
        update_coefficients();
    }

    float operator()(const float in) {
        buf0_ += f * (in - buf0_ + feedback * (buf0_ - buf1_));
        buf1_ += f * (buf0_ - buf1_);
        switch (type_) {
        case filter_type::lowpass:  return buf1_;
        case filter_type::bandpass: return buf0_ - buf1_;
        case filter_type::highpass: return in - buf0_;
        }
        assert(false);
        return 0.0f;
    }

public:
    // Parameters
    filter_type type_ = filter_type::lowpass;
    float freq_ = 0;
    float resonance_ = 0.5f;

    // State
    float buf0_ = 0.0f;
    float buf1_ = 0.0f;

    // Coefficients
    float f = 0;
    float feedback = 0;

    void update_coefficients() {
        f =  2*sin(pi*freq_/samplerate);
        feedback = resonance_ + resonance_/(1 - f);
    }
};

} // namespace splay

#endif
