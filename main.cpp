#include "wavedev.h"
#include "constants.h"
#include "midi.h"
#include "note.h"
#include "filter.h"
#include <vector>
#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <cassert>
#include <iostream>

namespace splay {

struct stereo_sample {
    float l;
    float r;
};
constexpr stereo_sample operator*(stereo_sample s, float scale) {
    return { s.l * scale, s.r * scale };
}
constexpr stereo_sample operator*(float scale, stereo_sample s) {
    return s * scale;
}

using signal_source = std::function<float(void)>;
using signal_sink   = std::function<void(float)>;
using sample_source = std::function<stereo_sample(void)>;

class output_dev {
public:
    using on_out_callback = std::function<void(std::vector<short>)>;

    explicit output_dev(const sample_source& main_generator, const on_out_callback& on_out_callback = nullptr) : main_generator_(main_generator), on_out_callback_(on_out_callback), wavedev_(samplerate, [this](short* d, size_t s) { do_mix(d, static_cast<int>(s/2)); }) {
    }
    output_dev(const output_dev&) = delete;
    output_dev& operator=(const output_dev&) = delete;

private:
    sample_source   main_generator_;
    on_out_callback on_out_callback_;
    wavedev         wavedev_;

    static short float_to_short(float f) {
        int i = static_cast<int>(f);
        if (i < std::numeric_limits<short>::min()) i = std::numeric_limits<short>::min();
        if (i > std::numeric_limits<short>::max()) i = std::numeric_limits<short>::max();
        return static_cast<short>(i);
    }

    void do_mix(short* d, int num_stereo_samples) {
        for (int i = 0; i < num_stereo_samples; ++i) {
            auto s = main_generator_();
            d[2 * i + 0] = float_to_short(s.l * 32767.0f);
            d[2 * i + 1] = float_to_short(s.r * 32767.0f);
        }
        if (on_out_callback_) on_out_callback_(std::vector<short>(d, d+num_stereo_samples));
    }
};


enum class waveform { sine, square, triangle, sawtooth, waveform_count };

class oscillator {
public:
    oscillator() {
    }

    void waveform(waveform wf) {
        waveform_ = wf;
    }

    void freq(float freq) {
        freq_ = freq;
    }

    void ang(float a) {
        t_ = a;
    }

    float operator()() {
        float val = 0;

        switch (waveform_) {
        case waveform::sine:
            val = cos(2.0f * pi * t_);
            break;
        case waveform::square:
            val = cos(2.0f * pi * t_);
            val = val < 0 ? -1.0f : 1.0f;
            break;
        case waveform::triangle:
            val = 2 * abs(2*(t_ - floor(t_+0.5f))) - 1;
            break;
        case waveform::sawtooth:
            val = 2 * (t_ - floor(t_ + 0.5f));
            break;
        default:
            assert(false);
        }
        t_ += freq_ / samplerate;
        return val;
    }

private:
    enum class waveform waveform_ = waveform::sawtooth;
    float freq_        = 0.0f;
    float t_           = 0.0f;
};


class sine_generator {
public:
    sine_generator() = default;
    sine_generator(const sine_generator&) = delete;
    sine_generator& operator=(const sine_generator&) = delete;

    void ang(float a) { ang_ = a; }
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
    float peak_level    = 0.9f;
    float sustain_level = 0.0001f;

    float attack_time   = 0.2f;
    float decay_time    = 0.8f;
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

    virtual void note_off(piano_key key, uint8_t) override {        
        if (const auto v = find_key(key)) {
            v->key_off();
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
        v->key_on(key, vel);
    }

    virtual void polyphonic_key_pressure(piano_key key, uint8_t pressure) {
        std::cout << "polyphonic_key_pressure " << piano_key_to_string(key) << " " << (int)pressure << std::endl;
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
        case midi::controller_type::sound_controller5:
        case midi::controller_type::effects1:
        case midi::controller_type::effects2:
        case midi::controller_type::effects3:
        case midi::controller_type::effects4:
        case midi::controller_type::effects5:
            break;
        default:
            int c = static_cast<int>(controller);
            const bool ignore = (c == 0) || (c >= 0x20 && c <= 0x3F) || (c >= 0x60 && c <= 0x77);
            if (!ignore) {
                std::cout << "Ignoring controller 0x" << std::hex << int(controller) << std::dec << " value " << int(value) << std::endl;
                assert(false);
            }
        }
    }
    virtual void program_change(uint8_t program) override {
        (void)program;
        //std::cout << "program_change " << int(program) << std::endl;
    }

    virtual void pitch_bend(int value) override {
        (void)value;//std::cout << "pitch_bend " << value << std::endl;
    }

    stereo_sample operator()() {
        float out = 0.0f;
        for (auto& v : voices) {
            out += v();
        }
        return pan_(out * volume_() * 10.0f / max_polyphony);
    }

private:

    struct voice {
        voice() {
            filter_.filter(filter_type::lowpass);
            filter_.cutoff_frequeny(15000.0f);            
        }

        void key_on(piano_key key, uint8_t vel) {
            assert(key != piano_key::OFF);
            assert(vel);
            key_ = key;
            vel_ = vel;
            //freq_(piano_key_to_freq(key_));
            osc_.freq(piano_key_to_freq(key_));
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

            //osc_.freq(freq_());
            auto out = osc_();
            out = filter_(out);
            out = envelope_(out);
            if (envelope_.is_off()) osc_.ang(0.0f);
            return out;
        }

        static bool compare_samples_played(const voice& l, const voice& r) {
            return l.samples_played_ < r.samples_played_;
        }
    private:
        static constexpr float min_freq = 0.001f;

        signal_envelope  envelope_;
        oscillator       osc_;
        piano_key        key_ = piano_key::OFF;
        //exp_ramped_value freq_{0.000001f, 0.001f, 20000.0f, 1.0f};
        biquad_filter    filter_;
        uint8_t          vel_ = 0;
        int              samples_played_ = 0;
    };

    static constexpr int  max_polyphony = 32;
    voice                 voices[max_polyphony];
    exp_ramped_value      volume_{0.000001f, 1.0f, 1.0f, 0.2f};
    panning_device        pan_;

    voice* find_key(piano_key key) {
        auto it = std::find_if(std::begin(voices), std::end(voices), [key](const voice& v) { return v.key() == key; });
        return it == std::end(voices) ? nullptr : it;
    }
};

class midi_player_0 {
public:
    explicit midi_player_0(std::istream& in) : p_(in) {
        for (int i = 0; i < midi::max_channels; ++i) {
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
        constexpr float boost = 50.0f; // Watch for loud (see below)
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
    midi::player          p_;
    simple_midi_channel   channels_[midi::max_channels];
};

} // namespace splay


template<typename X, typename Arg>
auto makesink(X& x, void (X::*func)(Arg))
{
    return std::bind(func, std::ref(x), std::placeholders::_1);
}

#include <conio.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <condition_variable>
#include "gui.h"
#include "vis.h"
#include "job_queue.h"

using namespace splay;



int main(int argc, const char* argv[])
{
    try {
        std::string filename;
        //filename = "../data/onestop.mid";
        //filename = "../data/A_natural_minor_scale_ascending_and_descending.mid";
        //filename = "../data/Characteristic_rock_drum_pattern.mid";
        //filename = "../data/beethoven_ode_to_joy.mid";
        filename = "../data/Beethoven_Ludwig_van_-_Beethoven_Symphony_No._5_4th.mid";
        //filename = "../data/Led_Zeppelin_-_Stairway_to_Heaven.mid";
        //filename = "../data/Blue_Oyster_Cult_-_Don't_Fear_the_Reaper.mid";
        if (argc >= 2) filename = argv[1];
        std::ifstream in(filename, std::ifstream::binary);
        if (!in) throw std::runtime_error("File not found: " + filename);
        midi_player_0 p{in};

        gui g{1000, 400};
        std::mutex data_mutex;
        std::condition_variable data_cv;
        std::vector<short> data;
        auto& spec_bitmap = g.make_bitmap_window(0, 0, 400, 300);
        auto& max_freq_label = g.make_label("", 0, 300, 400, 100);
        spectrum_analyzer spec_an{};
        auto& wave_bitmap = g.make_bitmap_window(500, 0, 400, 300);

        g.set_on_idle([&]() {
            std::vector<short> d;
            {
                std::unique_lock<std::mutex> lock(data_mutex);
                data_cv.wait_for(lock, std::chrono::milliseconds(10), [&] { d = std::move(data); return !d.empty(); });
            }

            if (d.empty()) return;

            // Stero -> Mono
            assert(d.size() % 2 == 0);
            int s = static_cast<int>(d.size() / 2);
            for (int i = 0; i < s; ++i) {
                d[i] = (d[i*2] + d[i*2+1]) / 2;
            }
            d.resize(s);

            draw_waveform_data(wave_bitmap, d);
            double freq_max = spec_an.draw_spetrum_data(spec_bitmap, d);
            std::ostringstream oss;
            oss << "Maximum frequency: " << std::setw(5) << int(freq_max+0.5) << " Hz";
            max_freq_label.text(oss.str());
        });
        simple_midi_channel ch;
        job_queue sound_job_queue;
        bool sound_instrument_edit_mode = false;
        g.add_key_listener(
        [&] (bool pressed, int vk) {
            if (!pressed && vk == 13) {
                sound_job_queue.push([&sound_instrument_edit_mode] { sound_instrument_edit_mode = !sound_instrument_edit_mode; });
                return;
            }
            auto key = key_to_note(vk);
            if (key == piano_key::OFF) {
                return;
            }
            sound_job_queue.push([&ch, pressed, key] { pressed  ? ch.note_on(key, 0x40) : ch.note_off(key, 0x40); });
        });

        output_dev od{
        [&] { 
            if (sound_instrument_edit_mode) {
                return 10.0f*ch();
            } else {
                return p();
            }
        },
        [&](std::vector<short> new_data) {
            {
                std::lock_guard<std::mutex> lock(data_mutex);
                data = std::move(new_data); 
            }
            sound_job_queue.execute_all();
        }};
        g.main_loop();
    } catch (const std::exception& e) {
        std::cout << e.what() << std::endl;
    }
}
