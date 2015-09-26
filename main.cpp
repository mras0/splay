#include "wavedev.h"
#include <vector>
#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <cassert>

namespace splay {

constexpr int samplerate = 44100;
constexpr float pi = 3.1415926535897932385f;

struct stereo_sample {
    float l;
    float r;
};

/*
enum class event_type {
    note_off, note_on
};

struct controller_event {
    event_type type;
    uint8_t arg1, arg2;
};

class controller_event_consumer {
public:
    void consume(const controller_event& e) {
        switch (e.type) {
        case event_type::note_off:
            assert(e.arg1 >= 1 && e.arg1 <= 127);
            assert(e.arg2 >= 1 && e.arg2 <= 127);
            on_note_off(e.arg1, e.arg2);
            break;
        case event_type::note_on:
            assert(e.arg1 >= 1 && e.arg1 <= 127);
            assert(e.arg2 >= 1 && e.arg2 <= 127);
            on_note_on(e.arg1, e.arg2);
            break;
        default:
            assert(false);
        }
    }
private:
    virtual void on_note_off(uint8_t note, uint8_t velocity) {(void)note;(void)velocity;}
    virtual void on_note_on(uint8_t note, uint8_t velocity) {(void)note;(void)velocity;}
};
*/

using signal_source = std::function<float(void)>;
using signal_sink   = std::function<void(float)>;
using sample_source = std::function<stereo_sample(void)>;
using tick_sink     = std::function<void(void)>;

class output_dev {
public:
    explicit output_dev(const sample_source& main_generator, const tick_sink& tick_listener) : main_generator_(main_generator), tick_listener_(tick_listener), wavedev_(samplerate, [this](short* d, size_t s) { do_mix(d, static_cast<int>(s/2)); }) {
    }
    output_dev(const output_dev&) = delete;
    output_dev& operator=(const output_dev&) = delete;

private:
    sample_source main_generator_;
    tick_sink     tick_listener_;
    wavedev       wavedev_;

    static short float_to_short(float f) {
        int i = static_cast<int>(f);
        if (i < std::numeric_limits<short>::min()) i = std::numeric_limits<short>::min();
        if (i > std::numeric_limits<short>::max()) i = std::numeric_limits<short>::max();
        return static_cast<short>(i);
    }

    void do_mix(short* d, int num_stereo_samples) {
        for (int i = 0; i < num_stereo_samples; ++i) {
            tick_listener_();
            auto s = main_generator_();
            d[2 * i + 0] = float_to_short(s.l * 32767.0f);
            d[2 * i + 1] = float_to_short(s.r * 32767.0f);
        }
    }
};

class sine_generator {
public:
    sine_generator() = default;
    sine_generator(const sine_generator&) = delete;
    sine_generator& operator=(const sine_generator&) = delete;

    void freq(float f) { freq_ = f; }

    float operator()() {
        const auto val = cos(ang_);
        ang_ += 2.0f * pi * freq_ / samplerate;

        assert(freq_ >= 0.0f);
        while (ang_ > 2.0f * pi) {
            ang_ -= 2.0f * pi;
        }
        return val;
    }

private:
    // State
    float ang_ = 0.0f;

    // Parameters
    float freq_ = 440.0f;
};

class signal_envelope {
public:
    constexpr static float min_level = 1.0f / 32767.0f;

    signal_envelope() = default;
    signal_envelope(const signal_envelope&) = delete;
    signal_envelope& operator=(const signal_envelope&) = delete;

    void key_on() {
        state = state_attack;
        set_multiplier(min_level, peak_level, attack_time);
    }

    void key_off() {
        if (state != state_off) {
            state = state_release;
            set_multiplier(sustain_level, min_level, release_time);
        }
    }

    float operator()(float in) {
        switch (state) {
        case state_attack:
            {
                assert(level);
                level *= multiplier;
                if (level >= peak_level) {
                    state = state_decay;
                    level = peak_level;
                    set_multiplier(peak_level, sustain_level, decay_time);
                }
            }
            break;
        case state_decay:
            {
                level *= multiplier;
                if (level <= sustain_level) {
                    state = state_sustain;
                    level = sustain_level;
                }
            }
            break;
        case state_sustain:
            level = sustain_level;
            break;
        case state_release:
            {
                level *= multiplier;
                if (level <= min_level) {
                    level = min_level;
                    state = state_off;
                }
            }
            break;
        default:
            assert(false);
        case state_off:
            level = min_level;
            return 0.0f;
        }

#if 0
        static size_t ticks = 0;
        static auto s = state_off;
        if (s != state) {
            static const char* n[] ={"state_off", "state_attack", "state_decay", "state_sustain", "state_release"};
            debug_output_stream << ticks/(double)rate << " " << n[state] << " " << level << " " << multiplier << std::endl;
            s = state;
            ticks = 0;
        }
        ticks++;
#endif
        assert(level >= min_level && level <= peak_level);
        return in * level;
    }

private:
    // State
    enum { state_off, state_attack, state_decay, state_sustain, state_release } state = state_off;
    float level         = min_level;
    float multiplier    = 0.0f;

    // Parameters
    float peak_level    = 1.0f;
    float sustain_level = 0.4f;

    float attack_time   = 0.02f;
    float decay_time    = 0.02f;
    float release_time  = 0.05f;

    // http://www.martin-finke.de/blog/articles/audio-plugins-011-envelopes/
    float calc_multiplier(float start_level, float end_level, float length) {
        assert(start_level > 0.0f);
        return 1.0f + (log(end_level) - log(start_level)) / (length * samplerate);
    }

    void set_multiplier(float start_level, float end_level, float length) {
        assert(start_level > 0.0f);
        assert(end_level > 0.0f);
        assert(length > 0.0f);
        if (level < min_level) level = min_level;
        multiplier = calc_multiplier(start_level, end_level, length);
    }
};

class panning_device {
public:
    panning_device() = default;
    panning_device(const panning_device&) = delete;
    panning_device& operator=(const panning_device&) = delete;

    stereo_sample operator()(float in) {
        // For actual panning see: Default Pan Formula http://www.midi.org/techspecs/rp36.php
        return { in, in };
    }
};

// https://en.wikipedia.org/wiki/Piano_key_frequencies
// A-4 (440 Hz) is the 49th key on an idealized keyboard
constexpr int   notes_per_octave = 12;

enum class piano_key : uint8_t {
    A_0 = 1,
   
    A_4 = 49,
    AS4,
    B_4,
    C_4,
    CS4,
    D_4,
    DS4,
    E_4,
    F_4,
    FS4,
    G_4,
    GS4,

    C_8 = 88,
};
constexpr piano_key operator+(piano_key lhs, int rhs) {
    return static_cast<piano_key>(static_cast<int>(lhs) + rhs);
}
constexpr piano_key operator+(int lhs, piano_key rhs) {
    return rhs + lhs;
}
static_assert(piano_key::GS4 == piano_key::A_4 + 11, "");

float note_difference_to_scale(int note_diff)
{
    // To go up a semi-tone multiply the frequency by pow(2,1./12) ~1.06
    return pow(2.0f, static_cast<float>(note_diff) / notes_per_octave);
}

constexpr bool piano_key_valid(piano_key n)
{
    return n >= piano_key::A_0 && n <= piano_key::C_8;
}

float piano_key_to_freq(piano_key n)
{
    assert(piano_key_valid(n));
    constexpr float A4_frequency = 440.0f;
    return A4_frequency * note_difference_to_scale(static_cast<int>(n) - static_cast<int>(piano_key::A_4));
}

std::string piano_key_to_string(piano_key n)
{
    assert(piano_key_valid(n));
    const int val    = static_cast<int>(n);
    const int octave = (val-1)/12;
    const int note   = (val-1)%12;
    static const char* const note_names[12] ={"A-", "A#", "B-", "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#"};
    return note_names[note] + std::to_string(octave);
}

class test_note_player {
public:
    explicit test_note_player(signal_envelope& envelope, signal_sink freq_out) : envelope_(envelope), freq_out_(freq_out) {
    }

    test_note_player(const test_note_player&) = delete;
    test_note_player& operator=(const test_note_player&) = delete;

    void tick() {
        while (time_to_next_tick_ <= 0.0f) {
            if (tick_ % 2 == 0) {
                //static const piano_key song[] = { piano_key::C_4, piano_key::C_4 + notes_per_octave, piano_key::DS4, piano_key::F_4, piano_key::G_4, piano_key::AS4 };
                const int song_len = 8;
                const auto tonic = piano_key::A_4 + (((tick_/2) / song_len) % notes_per_octave);

                // Major scale: W W h W W h (W = whole-step, h = half-step)
                //const piano_key song[song_len] = { tonic, tonic+2, tonic+4, tonic+5, tonic+7, tonic+9, tonic+11, tonic+12 };
                // Minor scale: W h W W h W W
                const piano_key song[song_len] ={ tonic, tonic+2, tonic+3, tonic+5, tonic+7, tonic+8, tonic+10, tonic+12};
                auto key = song[(tick_/2) % song_len];
                auto freq = piano_key_to_freq(key);
                printf("%s -- %s %f\n", piano_key_to_string(tonic).c_str(), piano_key_to_string(key).c_str(), freq);
                freq_out_(freq);
                envelope_.key_on();
            } else {
                envelope_.key_off();
            }
            tick_++;

            constexpr float bpm = 200.0f * 2;
            time_to_next_tick_ += 1.0f / (bpm / 60.0f);
        }

        time_to_next_tick_ -= 1.0f / samplerate;
    }
private:
    signal_envelope& envelope_;
    signal_sink      freq_out_;
    int              tick_ = 0;
    float            time_to_next_tick_ = 0.0f;
};

} // namespace splay


template<typename X, typename Arg>
auto makesink(X& x, void (X::*func)(Arg))
{
    return std::bind(func, std::ref(x), std::placeholders::_1);
}

#include <conio.h>
int main()
{
    printf("Playing. Press any key to exit\n");

    using namespace splay;
    sine_generator sine;
    signal_envelope env;
    panning_device pan;

    test_note_player p{env, makesink(sine, &sine_generator::freq)};
    sample_source x = [&] { return pan(env(sine())); };
    output_dev od{x, [&] { p.tick(); }};

    _getch();
}