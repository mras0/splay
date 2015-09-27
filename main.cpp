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

// http://www.martin-finke.de/blog/articles/audio-plugins-011-envelopes/
float calc_exp_multiplier(float start_level, float end_level, float length) {
    assert(start_level > 0.0f);
    return 1.0f + (log(end_level) - log(start_level)) / (length * samplerate);
}

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
    float peak_level    = 0.6f;
    float sustain_level = 0.2f;

    float attack_time   = 0.1f;
    float decay_time    = 0.05f;
    float release_time  = 0.1f;


    void set_multiplier(float start_level, float end_level, float length) {
        assert(start_level > 0.0f);
        assert(end_level > 0.0f);
        assert(length > 0.0f);
        assert(level >= min_level); //if (level < min_level) level = min_level;
        multiplier = calc_exp_multiplier(start_level, end_level, length);
    }
};

class exp_ramped_value {
public:
    explicit exp_ramped_value(float min, float value, float max, float slide_length)
        : value_(value)
        , down_multiplier_(calc_exp_multiplier(max, min, slide_length))
        , up_multiplier_(calc_exp_multiplier(min, max, slide_length))
        , target_(value) {
    }

    void operator()(float value) {
        target_ = value;
        
    }

    float operator()() {
        if (target_ == value_) {
        } else if (target_ < value_) {
            value_ = std::max(value_ * down_multiplier_, target_);
        } else {
            value_ = std::min(value_ * up_multiplier_, target_);
        }
        return value_;
    }

private:
    float value_;
    float down_multiplier_;
    float up_multiplier_;
    float slide_length_;
    float target_;
};

class panning_device {
public:
    panning_device() = default;
    panning_device(const panning_device&) = delete;
    panning_device& operator=(const panning_device&) = delete;

    void pan(float p) {
        assert(p >= 0.0f && p <= 1.0f);
        pan_(p);
    }

    stereo_sample operator()(float in) {
        // For actual panning see: Default Pan Formula http://www.midi.org/techspecs/rp36.php
        const auto pan = pan_();
        return { in * (1.0f - pan), in * pan};
    }
private:
    exp_ramped_value pan_{0.000001f, 0.5f, 1.0f, 0.01f};
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

double curtime = 0.0f;

class simple_midi_channel : public midi::channel {
public:
    simple_midi_channel() {
    }

    simple_midi_channel(const simple_midi_channel&) = delete;
    simple_midi_channel& operator=(const simple_midi_channel&) = delete;

    int index = -1;

    virtual void note_off(piano_key key, uint8_t) override {        
        if (const auto v = find_key(key)) {
            //std::cout << curtime << std::endl;
            //std::cout << index << " " << (v-std::begin(voices)) << " " << piano_key_to_string(key) << " OFF" << std::endl;
            v->key_off();
        } else {
            assert(max_polyphony == 1);
        }
    }

    virtual void note_on(piano_key key, uint8_t vel) override {
        if (vel == 0) {
            note_off(key, 0);
            return;
        }

        // Find voice
        auto v = find_key(key);
        if (!v) { // If the key wasn't already being played
            auto it = std::find_if(std::begin(voices), std::end(voices), [](const voice& v) { return !v.active(); });
            if (it != std::end(voices)) {
                v = it;
            } else { // And we can't find an empty channel
                // Use the channel that's been playing the longest
                //std::cout << "Harvesting!\n";
                v = std::max_element(std::begin(voices), std::end(voices), &voice::compare_samples_played);
            }
        }
        //std::cout << curtime << std::endl;
        //std::cout << index << " " << (v-std::begin(voices)) << " " << piano_key_to_string(key) << " " << int(vel) << std::endl;
        v->key_on(key, vel);
    }

    virtual void polyphonic_key_pressure(piano_key key, uint8_t pressure) {
        std::cout << "polyphonic_key_pressure " << piano_key_to_string(key) << " " << (int)pressure << std::endl;
        (void)key;(void)pressure;      
    }
    virtual void controller_change(midi::controller_type controller, uint8_t value) override {
        switch (controller) {
        case midi::controller_type::volume:
            volume_(value / 127.0f);
            break;
        case midi::controller_type::pan:
            pan_.pan(value / 127.0f);
            break;
        case midi::controller_type::modulation_wheel:
        case midi::controller_type::damper_pedal:
        case midi::controller_type::effects1:
            break;
        default:
            int c = static_cast<int>(controller);
            if (c != 0 && c != 0x20) {
                std::cout << "Ignoring controller 0x" << std::hex << int(controller) << std::dec << " value " << int(value) << std::endl;
                assert(false);
            }
        }
    }
    virtual void program_change(uint8_t program) override {
        (void) program;
        std::cout << "program_change" << int(program) << std::endl;
    }

    virtual void pitch_bend(int value) override {
        (void )value;
    }

    stereo_sample operator()() {
        float out = 0.0f;
        for (auto& v : voices) {
            out += v();
        }
        return pan_(out * volume_() / max_polyphony);
    }

private:
    static constexpr int max_polyphony = 32;
    exp_ramped_value     volume_{0.000001f, 1.0f, 1.0f, 0.2f};
    panning_device       pan_;

    struct voice {
        void key_on(piano_key key, uint8_t vel) {
            assert(key != piano_key::OFF);
            assert(vel);
            key_ = key;
            vel_ = vel;
            freq_(piano_key_to_freq(key_));
            samples_played_ = 0;
            envelope_.key_on();
        }

        void key_off() {
            envelope_.key_off();
        }

        piano_key key() const {
            return key_;
        }

        bool active() const {
            return key_ != piano_key::OFF && !envelope_.is_off();
        }

        float operator()() {
            ++samples_played_;

            if (!active()) {
                return 0.0f;
            }

            sine_.freq(freq_());
            return envelope_(sine_());
        }

        static bool compare_samples_played(const voice& l, const voice& r) {
            return l.samples_played_ < r.samples_played_;
        }
    private:
        static constexpr float min_freq = 0.001f;

        signal_envelope  envelope_;
        sine_generator   sine_;
        piano_key        key_ = piano_key::OFF;
        exp_ramped_value freq_{0.000001f, 0.001f, 200000.0f, 0.2f};
        uint8_t         vel_ = 0;
        int             samples_played_ = 0;
    } voices[max_polyphony];

    voice* find_key(piano_key key) {
        auto it = std::find_if(std::begin(voices), std::end(voices), [key](const voice& v) { return v.key() == key; });
        return it == std::end(voices) ? nullptr : it;
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
        curtime += 1.0f / samplerate;

        stereo_sample s{0.0f, 0.0f};
        for (auto& ch : channels_) {
            const auto ch_sample = ch();
            s.l += ch_sample.l;
            s.r += ch_sample.r;
        }
        constexpr float boost = 26.0f; // Watch for loud (see below)
        constexpr float scale = boost * 1.0f / midi::max_channels;
        s.l *= scale;
        s.r *= scale;

        if (fabs(s.l) > 1.0f || fabs(s.r) > 1.0f) {
            static bool warn = false;
            if (!warn) {
                warn = true;
                std::cout << "Loud!\n";
            }
        }
        return s;
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

int main(int argc, const char* argv[])
{
    try {
        std::string filename;
        //filename = "../data/onestop.mid";
        //filename = "../data/A_natural_minor_scale_ascending_and_descending.mid";
        //filename = "../data/Characteristic_rock_drum_pattern.mid";
        //filename = "../data/beethoven_ode_to_joy.mid";
        //filename = "../data/Beethoven_Ludwig_van_-_Beethoven_Symphony_No._5_4th.mid";
        filename = "../data/Led_Zeppelin_-_Stairway_to_Heaven.mid";
        if (argc >= 2) filename = argv[1];
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
