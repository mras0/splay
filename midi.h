#ifndef SPLAY_MIDI_H
#define SPLAY_MIDI_H

#include <iosfwd>
#include <memory>
#include <stdint.h>
#include "note.h"

namespace splay { namespace midi {

constexpr int max_channels = 16;

class channel {
public:
    virtual ~channel() = 0 {}

    virtual void note_off(piano_key key, uint8_t velocity) = 0;
    virtual void note_on(piano_key key, uint8_t velocity) = 0;
    virtual void polyphonic_key_pressure(piano_key key, uint8_t pressure) = 0;
    virtual void controller_change(uint8_t controller, uint8_t value) = 0;
    virtual void program_change(uint8_t program) = 0;
    virtual void pitch_bend(uint16_t value) = 0;
};

class player {
public:
    explicit player(std::istream& in);
    ~player();

    void set_channel(int index, channel& ch);
    void advance_time(float seconds);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};


} } // namespace splay::midi

#endif
