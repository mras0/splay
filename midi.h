#ifndef SPLAY_MIDI_H
#define SPLAY_MIDI_H

#include <iosfwd>
#include <memory>
#include <stdint.h>
#include "note.h"

namespace splay { namespace midi {

constexpr int max_channels = 16;

// http://www.midi.org/techspecs/midimessages.php
enum class controller_type : uint8_t {
    modulation_wheel = 0x01, // Modulation Wheel or Lever
    volume           = 0x07, // Volume
    pan              = 0x0A, // Pan
    damper_pedal     = 0x40, // Damper pedal on/off, <=63 off >=64 on
    effects1         = 0x5B, // Effects 1, Depth (default: Reverb Send Level - see MMA RP-023)

};

class channel {
public:
    virtual ~channel() = 0 {}

    virtual void note_off(piano_key key, uint8_t velocity) = 0;
    virtual void note_on(piano_key key, uint8_t velocity) = 0;
    virtual void polyphonic_key_pressure(piano_key key, uint8_t pressure) = 0;
    virtual void controller_change(controller_type controller, uint8_t value) = 0;
    virtual void program_change(uint8_t program) = 0;
    virtual void pitch_bend(int change) = 0;
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
