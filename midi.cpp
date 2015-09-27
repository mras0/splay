#include "midi.h"
#include "note.h"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <stdint.h>
#include <assert.h>

namespace splay { namespace midi {

piano_key convert_note(int key)
{
    constexpr int midi_a4 = 69;
    const auto pkey = piano_key::A_4 + (key - midi_a4);
    assert(piano_key_valid(pkey));
    return pkey;
}

constexpr uint16_t pack_u16(char a, char b)
{
    return (static_cast<uint16_t>(static_cast<uint8_t>(a)) << 8) | static_cast<uint8_t>(b);
}

constexpr uint32_t pack_u32(char a, char b, char c, char d)
{
    return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8) | static_cast<uint8_t>(d);
}

uint16_t read_be_u16(std::istream& in)
{
    char data[2];
    in.read(data, sizeof(data));
    return pack_u16(data[0], data[1]);
}

uint32_t read_be_u32(std::istream& in)
{
    char data[4];
    in.read(data, sizeof(data));
    return pack_u32(data[0], data[1], data[2], data[3]);
}

uint32_t read_var_num(std::istream& in)
{
    // Maximamlly allowed number is 0x0FFFFFFF
    uint32_t result = 0;
    for (int n = 0; n < 4; ++n) {
        int ch = in.get();
        assert(ch >= 0); // TODO: Handle failure better
        result <<= 7;
        result |= ch & 0x7f;
        if (!(ch & 0x80)) break;
        assert(n != 3); // TODO: Handle failure better
    }
    return result;
}

class chunk_type {
public:
    constexpr chunk_type() = default;
    constexpr explicit chunk_type(uint32_t repr) : repr_(repr) {}
    constexpr chunk_type(char a, char b, char c, char d) : repr_(pack_u32(a, b, c, d)) {}

    constexpr uint32_t repr() const { return repr_; }

private:
    uint32_t repr_;
};

constexpr chunk_type header_chunk_type{pack_u32('M', 'T', 'h', 'd')};
constexpr chunk_type track_chunk_type{pack_u32('M', 'T', 'r', 'k')};

std::ostream& operator<<(std::ostream& os, chunk_type t)
{
    const auto r = t.repr();
    return os << static_cast<char>((r >> 24) & 0xff) << static_cast<char>((r >> 16) & 0xff) << static_cast<char>((r >> 8) & 0xff) << static_cast<char>(r & 0xff);
}

bool operator==(chunk_type a, chunk_type b) { return a.repr() == b.repr(); }
bool operator!=(chunk_type a, chunk_type b) { return !(a == b); }

struct chunk_header {
    chunk_type type;
    uint32_t   length;
};

std::ostream& operator<<(std::ostream& os, const chunk_header& header)
{
    return os << '<' << header.type << ' ' << header.length << '>';
}

chunk_header read_chunk_header(std::istream& in)
{
    const chunk_type type{read_be_u32(in)};
    const uint32_t length = read_be_u32(in);
    if (!in) return { chunk_type{0}, 0 };

    return { type, length };
}

struct event {
    static constexpr uint8_t max_data_size = 15;

    int      time;
    uint16_t command;
    uint8_t  data_size;
    uint8_t  data[max_data_size];
};

std::ostream& operator<<(std::ostream& os, const event& e)
{
    const auto fill  = os.fill();
    const auto flags = os.flags();
    os << std::setw(6) << e.time << " ";
    os << std::hex << std::setfill('0');
    os << std::setw(4) << (int)e.command;
    for (int i = 0; i < e.data_size; ++i) {
        os << ' ' << std::setw(2) << (int)e.data[i];
    }
    os.flags(flags);
    os.fill(fill);
    return os;
}

struct track {
    std::vector<event> events;
    std::string        text_;
};

track read_track(std::istream& in)
{
    const auto track_header = read_chunk_header(in);
    if (track_header.length == 0 || !in) {
        assert(track_header.type == chunk_type{0});
        throw std::runtime_error("Unexpected EOF");
    }

    if (track_header.type != track_chunk_type) {
        std::ostringstream oss;
        oss << "Invalid track header " << track_header;
        throw std::runtime_error(oss.str());
    }

    track t{};

    const auto expected_end = in.tellg() + static_cast<std::ios::streamoff>(track_header.length);

    int current_time = 0;
    uint8_t last_message = 0;
    while (in.tellg() < expected_end) {
        current_time += read_var_num(in);

        const int command_byte = in.peek();
        if ((command_byte & 0xF0) == 0xF0) {
            in.get(); // consume

            // Sys event
            if (command_byte == 0xFF) { // Meta event
                const auto meta_event_type = in.get();
                assert(meta_event_type >= 0 && meta_event_type <= 0x7F);
                const auto meta_event_length = read_var_num(in);
                
                event e;
                e.time      = current_time;
                e.command   = static_cast<uint16_t>(0xFF00 | meta_event_type);
                e.data_size = static_cast<uint8_t>(std::min<uint32_t>(event::max_data_size, meta_event_length));
                in.read(reinterpret_cast<char*>(e.data), e.data_size);
                if (e.data_size != meta_event_length) {
                    in.seekg(meta_event_length - e.data_size, std::ios::cur);
                }
                t.events.push_back(e);
            } else {
                std::cout << "Unsupported system event 0x" << std::hex << std::setw(2) << std::setfill('0') << command_byte << std::dec << std::setfill(' ') << std::endl;
                assert(false);
            }
        } else {
            // Channel message
            if (command_byte & 0x80) {
                last_message = static_cast<uint8_t>(command_byte);
                in.get(); // consume
            }
            assert(last_message >= 0x80 && last_message <= 0xEF);

            event e;
            e.time      = current_time;
            e.command   = last_message;
            e.data_size = (last_message>>4 == 0xC || last_message>>4 == 0x0D) ? 1 : 2;
            for (int i = 0; i < e.data_size; ++i) {
                auto x = in.get();
                assert(x >= 0 && x <= 0x7F);
                e.data[i] = static_cast<uint8_t>(x);
            }

            t.events.push_back(e);
        }
    }

    return t;
}

class player::impl {
public:
    explicit impl(std::istream& in);

    void advance_time(float seconds) {
        assert(seconds > 0.0f && seconds < 1.0f);
        us_to_next_tick_ -= static_cast<int>(seconds * 1e6f + 0.5f);
        while (us_to_next_tick_ <= 0) {
            tick();
            us_to_next_tick_ += us_per_quater_note_ / division_;
        }
    }
    void tick();

    void set_channel(int index, channel& ch) {
        channels_[index] = &ch;
    }

private:
    std::vector<track> tracks_;
    int                division_           = 0; // delta divisions / quaternote
    int                current_tick_       = 0;
    int                us_to_next_tick_    = 0;
    int                us_per_quater_note_ = 500000; // 0.5s/quater-note = 1minute / 30quater-notes = 1minute / 120beats
    channel*           channels_[max_channels] = {};
    
    // 120 BPM = 30 quater-notes / minute = 0.5 quater-notes / second

    // Time signature deafults: 4/4 BPM=120
    // 1 beat = 1 1/16 note = 6 MIDI clock pulses
    // 4 beat = 1 quater-note = 24 MIDI clocks
    // 120 bpm = 120 * 6 MIDI clock pulses / minute = 12 MIDI clock pulses second
    
};

player::impl::impl(std::istream& in)
{
    const auto midi_header = read_chunk_header(in);
    if (midi_header.type != header_chunk_type || midi_header.length != 6) {
        std::ostringstream oss;
        oss << "Invalid MIDI header " << midi_header;
        throw std::runtime_error(oss.str());
    }
    const uint16_t midi_format    = read_be_u16(in);
    const int16_t  midi_tracks    = static_cast<int16_t>(read_be_u16(in));
    const uint16_t midi_divisions = read_be_u16(in);

    if (midi_format != 1) {
        throw std::runtime_error("Unsupported MIDI format " + std::to_string(midi_format));
    }

    std::cout << "Format: " << midi_format << " Tracks: " << midi_tracks << " Divisions: " << midi_divisions << std::endl;

    division_ = static_cast<int16_t>(midi_divisions); // If bit 15 of <division> is zero, the bits 14 thru 0 represent the number of delta time "ticks" which make up a quarter-note.
    assert(division_ > 0.0f);

    for (uint16_t track_number = 0; track_number < midi_tracks; ++track_number) {
        tracks_.push_back(read_track(in));
    }
}

void player::impl::tick()
{
    for (int track_number = 0; track_number < tracks_.size(); ++track_number) {

        for (const auto& e : tracks_[track_number].events) {
            if (e.time < current_tick_) continue;
            if (e.time > current_tick_) continue;
            assert(e.time == current_tick_);

            if (e.command < 0x100) {
                const auto event_type    = e.command >> 4;
                const auto channel_index = e.command & 0xf;
                if (!channels_[channel_index]) {
                    assert(false);
                    continue;
                }
                auto& channel = *channels_[channel_index];

                assert(event_type >= 8 && event_type <= 14);
                switch (event_type) {
                case 0x08: // Note off
                    assert(e.data_size == 2);
                    channel.note_off(convert_note(e.data[0]), e.data[1]);
                    break;
                case 0x09: // Note on
                    assert(e.data_size == 2);
                    channel.note_on(convert_note(e.data[0]), e.data[1]);
                    break;
                case 0x0A: // Key after-touch
                    assert(e.data_size == 2);
                    channel.polyphonic_key_pressure(convert_note(e.data[0]), e.data[1]);
                    break;
                case 0x0B: // Controller change
                    assert(e.data_size == 2);
                    if (e.data[0] < 120) {
                        channel.controller_change(static_cast<controller_type>(e.data[0]), e.data[1]);
                    } else if (e.data[0] == 120) {
                        std::cout << "All sound off " << (int)e.data[1] << std::endl;
                    } else if (e.data[0] == 121) {
                        std::cout << "Reset all controllers " << (int)e.data[1] << std::endl;
                    } else {
                        std::cout << "Unsupported controller change " << (int)e.data[0] << " " << (int)e.data[1] << "\n";
                        assert(false);
                    }
                    break;
                case 0xC: // Program (patch) change
                    assert(e.data_size == 1);
                    channel.program_change(e.data[0]);
                    break;
                case 0xD: // Channel pressure (after-touch)
                    assert(e.data_size == 1);
                    assert(false);
                    break;
                case 0xE: // Pitch bend
                    {
                        assert(e.data_size == 2);
                        constexpr int center = 0x2000;
                        channel.pitch_bend(((e.data[0] << 7) | e.data[1]) - center);
                    }
                    break;
                default:
                    std::cout << "Ignoring event " << event_type << std::endl;
                    assert(false);
                }
                continue;
            }

            // Meta event
            assert(e.command>>8 == 0xff);
            switch (e.command & 0xff) {
            case 0x01: // FF 01 len text Text Event
                std::cout << "Text " << std::string(e.data, e.data+e.data_size) << std::endl;
                break;
            case 0x02: // FF 02 len text Copyright Notice
                std::cout << "Copyright " << std::string(e.data, e.data+e.data_size) << std::endl;
                break;
            case 0x03: // FF 03 len text Sequence/Track Name
                std::cout << "Track name (" << track_number << ") " << std::string(e.data, e.data+e.data_size) << std::endl;
                break;
            case 0x2F: // FF 2F 00 End of Track
                assert(e.data_size == 0);
                std::cout << "End of track " << track_number << std::endl;
                break;
            case 0x51: // FF 51 03 tttttt Set Tempo (in microseconds per MIDI quarter-note)
                {
                    assert(e.data_size == 3);
                    us_per_quater_note_ = (e.data[0]<<16) | (e.data[1]<<8) | e.data[2];
                    std::cout << "Set tempo " << us_per_quater_note_ << " us/midi-quater-note" << std::endl;
                }
                break;
            case 0x58: // FF 58 04 nn dd cc bb Time Signature
                {
                    // nn numerator
                    // dd denominator, negative power of two: 2 represents a quarter-note, 3 represents an eighth-note, etc.
                    // cc expresses the number of MIDI clocks in a metronome click
                    // bb expresses the number of notated 32nd-notes in a MIDI quarter-note (24 MIDI clocks, 1 beat=6 MIDI clocks)
                    assert(e.data_size == 4);
                    std::cout << int(e.data[0]) << "/" << int(1 << e.data[1]) << " -- " << int(e.data[2]) << " clocks/click -- " << int(e.data[3]) << " 32nd notes in quater note" << std::endl;
                }
                break;
            case 0x59: // FF 59 02 sf mi Key Signature
                {
                    assert(e.data_size == 2);
                    std::cout << "Key Signature C + " << int(e.data[0]) << " sharps, " << (e.data[1] ? "minor" : "major") << std::endl;
                }
                break;
            default:
                std::cout << e << std::endl;
                assert(false);
            }
        }
    }
    ++current_tick_;
}

player::player(std::istream& in) : impl_(new impl(in))
{
}

player::~player() = default;

void player::set_channel(int index, channel& ch)
{
    impl_->set_channel(index, ch);
}

void player::advance_time(float seconds)
{
    impl_->advance_time(seconds);
}

} } // namespace splay::midi