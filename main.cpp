#include "wavedev.h"
#include "midi.h"
#include "note.h"
#include <vector>
#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <cassert>
#include <iostream>

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
    explicit output_dev(const sample_source& main_generator, const tick_sink& tick_listener = nullptr) : main_generator_(main_generator), tick_listener_(tick_listener), wavedev_(samplerate, [this](short* d, size_t s) { do_mix(d, static_cast<int>(s/2)); }) {
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
            if (tick_listener_) tick_listener_();
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
        if (!is_off()) {
            state = state_release;
            set_multiplier(sustain_level, min_level, release_time);
        }
    }

    bool is_off() const {
        return state == state_off;
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
    float sustain_level = 0.01f;

    float attack_time   = 0.04f;
    float decay_time    = 0.3f;
    float release_time  = 0.01f;

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
                std::cout << piano_key_to_string(tonic) <<  " " << piano_key_to_string(key) << " " << freq << std::endl;
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

float curtime = 0.0f;

class simple_midi_channel : public midi::channel {
public:
    simple_midi_channel() {
        for (auto& n : note_) n = piano_key::OFF;
    }

    int index = 0;

    virtual void note_off(piano_key key, uint8_t) override {
        const auto ch = find_key(key);
        if (ch >= 0) {
            envelope_[ch].key_off();
        }
    }

    virtual void note_on(piano_key key, uint8_t) override {
        // Find channel
        auto ch = find_key(key);
        if (ch < 0) { // If the key wasn't already being played
            auto it = std::find_if(std::begin(envelope_), std::end(envelope_), [](const signal_envelope& e) { return e.is_off(); });
            if (it != std::end(envelope_)) {
                ch = static_cast<int>(it - std::begin(envelope_));
            } else { // And we can't find an empty channel
                // Use the channel that's been playing the longest
                ch = static_cast<int>(std::max_element(std::begin(samples_played_), std::end(samples_played_)) - std::begin(samples_played_));
            }
        }
        assert(ch < max_polyphony);
        const auto freq = piano_key_to_freq(key);
        note_[ch] = key;
        samples_played_[ch] = 0;
        sine_[ch].freq(freq);
        envelope_[ch].key_on();
    }

    virtual void polyphonic_key_pressure(piano_key key, uint8_t pressure) {
        (void)key;(void)pressure;      
    }
    virtual void controller_change(uint8_t controller, uint8_t value) override {
        (void)controller; (void) value; 
        //std::cout << curtime << " " << index << " Controller " << int(controller) << " value " << int(value) << std::endl;
    }
    virtual void program_change(uint8_t program) override {
        (void) program;
    }

    virtual void pitch_bend(uint16_t value) override {
        (void )value;
    }

    float operator()() {
        float out = 0.0f;
        int active = 0;
        for (int i = 0; i < max_polyphony; ++i) {
            if (!envelope_[i].is_off()) {
                out += envelope_[i](sine_[i]());
                samples_played_[i]++;
                ++active;
            }
        }
        if (!active) {
            return 0.0f;
        }
        return out / active;
    }

private:
    static constexpr int max_polyphony = 8;
    signal_envelope envelope_[max_polyphony];
    sine_generator  sine_[max_polyphony];
    piano_key       note_[max_polyphony];
    int             samples_played_[max_polyphony];

    int find_key(piano_key key) {
        for (int ch = 0; ch < max_polyphony; ++ch) {
            if (note_[ch] == key) {
                return ch;
            }
        }
        return -1;
    }
};

class midi_player_0 {
public:
    explicit midi_player_0(std::istream& in) : p_(in) {
        for (int i = 0; i < midi::max_channels; ++i) {
            channels_[i].index = i;
            p_.set_channel(i, channels_[i]);
        }
    }

    stereo_sample operator()() {
         p_.advance_time(1.0f / samplerate);

        float s = 0.0f;
        for (auto& ch : channels_) {
            s += ch();
        }
        s *= 1.0f / 16.0f;
        curtime += 1.0f / samplerate;

        return { s, s };
    }

private:
    midi::player        p_;
    simple_midi_channel channels_[midi::max_channels];
};

} // namespace splay


template<typename X, typename Arg>
auto makesink(X& x, void (X::*func)(Arg))
{
    return std::bind(func, std::ref(x), std::placeholders::_1);
}

#include <conio.h>
#include <fstream>

using namespace splay;

int main()
{
    try {
        const std::string filename = "../data/onestop.mid";
        //const std::string filename = "../data/A_natural_minor_scale_ascending_and_descending.mid";
        //const std::string filename = "../data/Characteristic_rock_drum_pattern.mid";
        std::ifstream in(filename, std::ifstream::binary);
        if (!in) throw std::runtime_error("File not found: " + filename);
        midi_player_0 p{in};


        //sine_generator sine;
        //signal_envelope env;
        //panning_device pan;
        //test_note_player p{env, makesink(sine, &sine_generator::freq)};
        //sample_source x = [&] { return pan(env(sine())); };
        output_dev od{[&]{ return p(); }};

        std::cout << "Playing. Press any key to exit\n";
        _getch();
    } catch (const std::exception& e) {
        std::cout << e.what() << std::endl;
    }
}
