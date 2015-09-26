#include "wavedev.h"
#include <vector>
#include <algorithm>
#include <cassert>

constexpr int samplerate = 44100;
constexpr float pi = 3.1415926535897932385f;

struct stereo_sample {
    float l;
    float r;
};

class voice {
public:
    virtual ~voice() {}

    virtual bool active() const { return true; }
    virtual float next() = 0;
};

class mixer {
public:
    using tick_function = std::function<void (mixer&)>;

    explicit mixer(const tick_function& tick) : tick_(tick), samples_to_next_tick_(0), wavedev_(samplerate, [this](short* d, size_t s) { do_mix(d, s); }) {
    }

    void add_voice(voice* v) { 
        voices_.push_back(v);
    }

    void remove_voice(voice* v) {
        auto it = std::find(begin(voices_), end(voices_), v);
        if (it == end(voices_)) {
            assert(false);
            return;
        }
        voices_.erase(it);
    }

private:
    tick_function       tick_;
    int                 samples_to_next_tick_;
    std::vector<voice*> voices_;
    wavedev             wavedev_;

    static int samples_per_tick() {
        return static_cast<int>(samplerate / 50.0f);
    }

    void do_mix(short* data, size_t num_samples) {
        std::vector<float> mixbuf(num_samples);

        const auto num_stereo_samples = static_cast<int>(num_samples / 2);
        auto mix_ptr = mixbuf.data();
        for (int samples_mixed = 0; samples_mixed < num_stereo_samples;) {
            if (samples_to_next_tick_ <= 0) {
                assert(samples_to_next_tick_ == 0);
                tick_(*this);
                samples_to_next_tick_ = samples_per_tick();
            }

            const int samples_here = std::min(samples_to_next_tick_, num_stereo_samples - samples_mixed);
            for (auto& v: voices_) {
                for (int i = 0; i < samples_here; ++i) {
                    const auto s = v->next();
                    mix_ptr[i * 2 + 0] += s;
                    mix_ptr[i * 2 + 1] += s;
                }
            }
            samples_to_next_tick_ -= samples_here;
            samples_mixed += samples_here;
            mix_ptr += 2 * samples_here;
        }
           
        const float volume_multiplier = voices_.size() ? 1.0f / static_cast<float>(voices_.size()) : 0.0f;
        for (size_t i = 0; i < num_samples; ++i) {
            auto val = mixbuf[i] * volume_multiplier;
            if (val < -1.0f) val = -1.0f;
            if (val >  1.0f) val =  1.0f;
            data[i] = static_cast<short>(val * 32767.0f);
        }
    }
};

class sine_voice : public voice {
public:
    explicit sine_voice(float freq) : freq_(freq) {}

    virtual float next() override {
        const auto val = cos(2.0f * pi * t_);
        t_ += freq_ / samplerate;
        return val;
    }

private:
    float freq_ = 440.0f;
    float t_ = 0.0f;
};

class volume_ramp : public voice {
public:
    explicit volume_ramp(std::unique_ptr<voice> in, float initial_volume, float multiplier) : in_(std::move(in)), volume_(initial_volume), multiplier_(multiplier) {
        assert(in_);
        assert(volume_ >= 0.0f);
        assert(multiplier_ >= 0.0f);
    }

    virtual float next() override {
        const auto val = in_->next() * volume_;
        volume_ *= multiplier_;
        return val;
    }

    virtual bool active() const override {
        return volume_ >= 1.0f/32767.0f;
    }
private:
    std::unique_ptr<voice> in_;
    float volume_;
    float multiplier_;
};

#include <stdio.h>

class player {
public:
    explicit player() {
    }

    void tick(mixer& m) {
        if (ticks_ == 0) {
            for (int i = 1; i <= 6; ++i) {
                voices_.emplace_back(std::make_unique<volume_ramp>(std::make_unique<sine_voice>(440.0f * i), 1.0f, 0.99999f));
                m.add_voice(voices_.back().get());
            }
        }

        for (auto i = static_cast<int>(voices_.size()) - 1; i >= 0; --i) {
            if (!voices_[i]->active()) {
                printf("Removing voice\n");
                m.remove_voice(voices_[i].get());
                voices_.erase(voices_.begin() + i);
            }
        }

        ++ticks_;
    }
private:
    std::vector<std::unique_ptr<voice>> voices_;
    int ticks_ = 0;
};

#include <conio.h>
int main()
{
    printf("Playing. Press any key to exit\n");
    player p{};
    mixer m{[&p](mixer&m){p.tick(m);}};
    _getch();
}
