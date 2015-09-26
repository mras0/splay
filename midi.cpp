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

class print_dev {
public:
    explicit print_dev(int d, int t) : d_(d), t_(t) {}
    int d_, t_;

    void h(int channel) const {
        std::cout << "Division " << d_ << " Track " << t_ << " ";
        std::cout << "Channel " << channel << " ";
    }

    virtual void note_off(uint8_t channel, piano_key key, uint8_t velocity) {
        h(channel);
        std::cout << "Note off          " << piano_key_to_string(key) << " " << (int)velocity << std::endl;
    }

    virtual void note_on(uint8_t channel, piano_key key, uint8_t velocity) {
        h(channel);
        std::cout << "Note on           " << piano_key_to_string(key) << " " << (int)velocity << std::endl;
    }

    virtual void polyphonic_key_pressure(uint8_t channel, piano_key key, uint8_t pressure) {
        h(channel);
        std::cout << "Key pressure      " << piano_key_to_string(key) << " " << (int)pressure << std::endl;
    }

    virtual void controller_change(uint8_t channel, uint8_t controller, uint8_t value) {
        h(channel);
        std::cout << "Controller change " << (int)controller << " " << (int)value << std::endl;
    }

    virtual void program_change(uint8_t channel, uint8_t program) {
        h(channel);
        std::cout << "Program change    " << (int)program << std::endl;
    }
    
    void do_controller_change(uint8_t channel, uint8_t controller, uint8_t value) {
        if (controller < 120) {
            controller_change(channel, controller, value);
        } else if (controller == 120) {
            std::cout << "All sound off" << std::endl;
        } else if (controller == 121) {
            std::cout << "Reset all controllers" << std::endl;
        } else {
            std::cout << "Unsupported controller change " << (int)controller << " " << (int)value << "\n";
            assert(false);
        }
    }
};

class track {
public:
    explicit track(int n, std::vector<std::uint8_t> data) : n_(n), data_(std::move(data)), pos_(0), last_message_(0), count_down_(-1) {
    }

    bool done() const;

    template<typename V>
    void next_division(V& visitor);
private:
    int                       n_;
    std::vector<std::uint8_t> data_;
    int                       pos_;
    int                       last_message_;
    int                       count_down_;

    bool available(int amm) const;
    void check_available(int amm) const;

    uint8_t  get_u8();
    uint32_t get_var_num();

    template<typename V>
    void do_meta_event(V& visitor, uint8_t type, const uint8_t* data, int length);
};

bool track::available(int amm) const
{
    return pos_ + amm <= data_.size();
}

void track::check_available(int amm) const
{
    if (!available(amm)) {
        assert(false);
        throw std::runtime_error("Unexpected end of track");
    }
}

uint8_t track::get_u8()
{
    check_available(1);
    return data_[pos_++];
}

uint32_t track::get_var_num()
{
    // Maximamlly allowed number is 0x0FFFFFFF
    uint32_t result = 0;
    for (int n = 0; n < 4; ++n) {
        const auto ch = get_u8();
        result <<= 7;
        result |= ch & 0x7f;
        if (!(ch & 0x80)) break;
        assert(n != 3); // TODO: Handle failure better
    }
    return result;
}

bool track::done() const
{
    return pos_ >= data_.size();
}
template<typename V>
void track::next_division(V& visitor)
{
    while (!done()) {
        if (count_down_ == -1) {
            count_down_ = get_var_num();
        }
        if (count_down_ > 0) {
            count_down_--;
            return;
        }

        assert(count_down_ == 0);
        check_available(1);
        const int command_byte = data_[pos_];
        if ((command_byte & 0xF0) == 0xF0) {
            pos_++; // consume

            // Sys event
            if (command_byte == 0xFF) { // Meta event
                check_available(1);
                const auto meta_event_type = get_u8();
                assert(meta_event_type >= 0 && meta_event_type <= 0x7F);
                const auto meta_event_length = get_var_num();
                check_available(meta_event_length);
                do_meta_event(visitor, meta_event_type, meta_event_length ? &data_[pos_] : nullptr, meta_event_length);
                pos_ += meta_event_length;
            } else {
                std::cout << "Unsupported system event 0x" << std::hex << std::setw(2) << std::setfill('0') << command_byte << std::dec << std::setfill(' ') << std::endl;
                assert(false);
            }
        } else {
            // Channel message
            if (command_byte & 0x80) {
                last_message_ = command_byte;
                pos_++; // consume
            }
            assert(last_message_ >= 0x80 && last_message_ <= 0xE0);

            const uint8_t channel = last_message_ & 0xf;

            switch (last_message_>>4) {
            case 0x8: // Note off
            case 0x9: // Note on
            case 0xA: // Key after-touch
                {
                    const auto key  = get_u8(); // key
                    const auto vel  = get_u8(); // velocity/pressure
                    assert((key&0x80) == 0);
                    assert((vel&0x80) == 0);
                    const int midi_a4 = 69;
                    const auto pkey = piano_key::A_4 + (key - midi_a4);
                    assert(piano_key_valid(pkey));
                    if (last_message_>>4 == 8)
                        visitor.note_off(channel, pkey, vel);
                    else if (last_message_>>4 == 9)
                        visitor.note_on(channel, pkey, vel);
                    else
                        visitor.polyphonic_key_pressure(channel, pkey, vel);
                }
                break;
            case 0xB: // Controller change
                {
                    const auto con = get_u8(); // controller
                    const auto val = get_u8(); // value
                    assert((con&0x80) == 0);
                    assert((val&0x80) == 0);
                    visitor.do_controller_change(channel, con, val);
                }
                break;
            case 0xC: // Program (patch) change
                {
                    const auto prog = get_u8();
                    assert((prog&0x80) == 0);
                    visitor.program_change(channel, prog);
                }
                break;
            default:
                std::cout << "Unhandled midi message: " << (last_message_>>4) << std::endl;
                assert(false);
            }
        }

        count_down_ = -1;
    }
}

template<typename V>
void track::do_meta_event(V& visitor, uint8_t type, const uint8_t* data, int length)
{
    (void)visitor;
    switch (type) {
    case 0x02: // FF 02 len text Copyright Notice
        std::cout << "Copyright " << std::string(data, data+length) << std::endl;
        break;
    case 0x03: // FF 03 len text Sequence/Track Name
        std::cout << "Track name " << std::string(data, data+length) << std::endl;
        break;
    case 0x2F: // FF 2F 00 End of Track
        if (length != 0) throw std::runtime_error("Invalid end eof track length=" + std::to_string(length));
        std::cout << "End of track" << std::endl;
        break;
    case 0x51: // FF 51 03 tttttt Set Tempo (in microseconds per MIDI quarter-note)
        {
            if (length != 3) throw std::runtime_error("Invalid tempo change length=" + std::to_string(length));
            const int tempo = (data[0]<<16) | (data[1]<<8) | data[2];
            std::cout << "Set tempo " << tempo << " us/midi-quater-note" << std::endl;
        }
        break;
    case 0x58: // FF 58 04 nn dd cc bb Time Signature
        {
            // nn numerator
            // dd denominator, negative power of two: 2 represents a quarter-note, 3 represents an eighth-note, etc.
            // cc expresses the number of MIDI clocks in a metronome click
            // bb expresses the number of notated 32nd-notes in a MIDI quarter-note (24 MIDI clocks, 1 beat=6 MIDI clocks)
            if (length != 4) throw std::runtime_error("Invalid time signature length=" + std::to_string(length));
            std::cout << int(data[0]) << "/" << int(1 << data[1]) << " -- " << int(data[2]) << " clocks/click -- " << int(data[3]) << " 32nd notes in quater note" << std::endl;
        }
        break;
    case 0x59: // FF 59 02 sf mi Key Signature
        {
            if (length != 2) throw std::runtime_error("Invalid key signature length=" + std::to_string(length));
            std::cout << "Key Signature C + " << int(data[0]) << " sharps, " << (data[1] ? "minor" : "major") << std::endl;
        }
        break;
    default:
        const char* hex = "0123456789ABCDEF";
        std::cout << hex[type>>4] << hex[type&0xf] << " len=" << length << std::endl;
        (void)data;
        assert(false);
    }
}

std::vector<uint8_t> read_track(std::istream& in)
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

    std::vector<uint8_t> data(track_header.length);
    in.read(reinterpret_cast<char*>(data.data()), data.size());
    return data;

#if 0
    int current_time = 0;
    int last_message = 0;
    while (in.tellg() < expected_end) {
        const auto delta_time = read_var_num(in);
        current_time += delta_time;
        std::cout << std::setw(6) << current_time << " " << std::flush;
        const int command_byte = in.peek();

        if ((command_byte & 0xF0) == 0xF0) {
            in.get();
            // Sys event
            if (command_byte == 0xFF) { // Meta event
                const int meta_event_type = in.get();
                assert(meta_event_type >= 0 && meta_event_type <= 0x7F);
                const auto meta_event_length = read_var_num(in);
                std::vector<char> data(meta_event_length);
                in.read(data.data(), data.size());
                std::cout << "Meta event: 0x" << std::hex << std::setw(2) << std::setfill('0') << meta_event_type << std::dec << " " << meta_event_length << std::endl;
                if (meta_event_type == 1 || meta_event_type == 2 || meta_event_type == 3) {
                    std::cout << " " << std::string(data.begin(), data.end()) << std::endl;
                } else {
                    assert(false);
                }
                std::cout << std::setfill(' ');
            } else {
                std::cout << "Unsupported system event 0x" << std::hex << std::setw(2) << std::setfill('0') << command_byte << std::dec << std::setfill(' ') << std::endl;
                assert(false);
            }
        } else {
            // Channel message
            if (command_byte & 0x80) {
                last_message = command_byte>>4;
                in.get();
            }
            assert(last_message >= 8 && last_message <= 14);

            static const char* const command_names[7] ={
                "Note off", "Note on", "Key after-touch", "Control Change", "Program (patch) change", "Channel after-touch", "Pitch wheel change"
            };
            std::cout << std::setfill('0') << std::setw(2) << std::hex << command_byte << " " << std::dec << std::setfill(' ');
            std::cout << std::left << std::setw(20) << command_names[last_message-8] << " " << std::right;

            switch (last_message) {
            case 0x8: // Note off
            case 0x9: // Note on
            case 0xA: // Key after-touch
            case 0xB: // Controller change
                {
                    const int val1 = in.get(); // key/controller
                    const int val2 = in.get(); // velocity/pressure/value
                    assert((val1&0x80) == 0);
                    assert((val2&0x80) == 0);
                    if (last_message == 8 || last_message == 9) {
                        if (val1 == 0) std::cout << "<off>";
                        else {
                            const int octave = (val1-1)/12;
                            const int note = (val1-1)%12;
                            static const char* const note_names[12] ={"A-", "A#", "B-", "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#"};
                            std::cout << note_names[note] << octave;
                        }
                        std::cout << " " << (int)val2;
                    } else {
                        if (last_message == 0x0B && val1 >= 120) {
                            switch (val1) {
                            case 120: std::cout << "All sound off"; break;    
                            case 121: std::cout << "Reset all controllers"; break;
                            default:
                                std::cout << "Unknown channel mode message " << (int)val1 << std::endl;
                            }
                        } else {
                            std::cout << std::hex << std::hex << std::setfill('0');
                            std::cout << "0x" << std::setw(2) << (int)val1 << " 0x" << std::setw(2) << (int)val2;
                            std::cout << std::dec << std::setfill(' ');
                        }
                    }

                    t.events.push_back(event{current_time, static_cast<event_type>(last_message), static_cast<uint8_t>(command_byte&0xf), static_cast<uint16_t>(val1), static_cast<uint16_t>(val2)});
                }
                break;
            case 0xC: // Program (patch) change
            case 0xD: // Channel pressure (after-touch)
                {
                    const int val1 = in.get();
                    assert((val1&0x80) == 0);
                    std::cout << std::hex << std::hex << std::setfill('0');
                    std::cout << "0x" << std::setw(2) << (int)val1;
                    std::cout << std::dec << std::setfill(' ');

                    t.events.push_back(event{current_time, static_cast<event_type>(last_message), static_cast<uint8_t>(command_byte&0xf), static_cast<uint16_t>(val1), 0});
                }
                break;
            case 0xE: // Pitch bend
                {
                    const int val1 = in.get();
                    const int val2 = in.get();
                    assert((val1&0x80) == 0);
                    assert((val2&0x80) == 0);
                    const int val = (val1<<7) | val2;
                    std::cout << std::hex << std::hex << std::setfill('0');
                    std::cout << "0x" << std::setw(4) << val;
                    std::cout << std::dec << std::setfill(' ');

                    t.events.push_back(event{current_time, static_cast<event_type>(last_message), static_cast<uint8_t>(command_byte&0xf), static_cast<uint16_t>(val), 0});
                }
                break;
            default:
                std::cout << std::flush;
                assert(false);
            }

            std::cout << std::endl;
        }
    }
#endif
}

class player::impl {
public:
    impl(std::istream& in);
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

    //std::cout << "Format: " << midi_format << " Tracks: " << midi_tracks << " Divisions: " << midi_divisions << std::endl;

    std::vector<track> tracks_;
    for (uint16_t track_number = 0; track_number < midi_tracks; ++track_number) {
        tracks_.emplace_back(track_number, read_track(in));
    }


    std::cout << "Divisions (delta time ticks per quater note): " << midi_divisions << std::endl;
    int division = 0;
    while (!std::all_of(begin(tracks_), end(tracks_), [](const midi::track& t) { return t.done(); })) {
        int i = 0;
        for (auto& t : tracks_) {
            print_dev p{division, i++};
            t.next_division(p);
        }
        ++division;
    }
}

player::player(std::istream& in) : impl_(new impl(in))
{
}

player::~player() = default;

} } // namespace splay::midi

/*
void test_read_var()
{
    auto test = [](const std::string& t, uint32_t res) { std::istringstream iss(t); assert(midi::read_var_num(iss) == res); };
    test(std::string(1, 0), 0);
    test(std::string(1, 0x7f), 0x7f);
    test("\x87\x68", 1000);
    test("\xFF\x7F", 16383);
    test("\xBD\x84\x40", 1000000);
    test("\xFF\xFF\xFF\x7F", 0x0FFFFFFF);
}

int main()
{
    test_read_var();
    try {
        using namespace midi;
        const std::string filename = "../data/Characteristic_rock_drum_pattern.mid";
        std::ifstream in(filename, std::ifstream::binary);
        if (!in) throw std::runtime_error("Could not open " + filename);

        const auto midi_header = read_chunk_header(in);
        if (midi_header.type != header_chunk_type || midi_header.length != 6) {
            std::ostringstream oss;
            oss << "Invalid MIDI header in " << filename << " " << midi_header;
            throw std::runtime_error(oss.str());
        }
        const uint16_t midi_format    = read_be_u16(in);
        const uint16_t midi_tracks    = read_be_u16(in);
        const uint16_t midi_divisions = read_be_u16(in);

        if (midi_format != 1) {
            throw std::runtime_error(filename + " has unsupported MIDI format " + std::to_string(midi_format));
        }

        std::cout << "Format: " << midi_format << " Tracks: " << midi_tracks << " Divisions: " << midi_divisions << std::endl;

        for (uint16_t track_number = 0; track_number < midi_tracks; ++track_number) {
            const auto track_header = read_chunk_header(in);
            if (track_header.length == 0 || !in) {
                assert(track_header.type == chunk_type{0});
                throw std::runtime_error("Unexpected EOF in " + filename);
            }

            std::cout << track_header << std::endl;
            if (track_header.type != track_chunk_type) {
                std::ostringstream oss;
                oss << "Invalid track header in " << filename << " " << track_header;
                throw std::runtime_error(oss.str());
            }

            const auto expected_end = in.tellg() + static_cast<std::ios::streamoff>(track_header.length);

            double current_time = 0;
            int last_message = 0;
            while (in.tellg() < expected_end) {
                const auto delta_time = read_var_num(in);
                current_time += delta_time;
                std::cout << std::setw(6) << current_time << " " << std::flush;
                const int command_byte = in.peek();

                if ((command_byte & 0xF0) == 0xF0) {
                    in.get();
                    // Sys event
                    if (command_byte == 0xFF) { // Meta event
                        const int meta_event_type = in.get();
                        assert(meta_event_type >= 0 && meta_event_type <= 0x7F);
                        const auto meta_event_length = read_var_num(in);
                        std::vector<char> data(meta_event_length);
                        in.read(data.data(), data.size());
                        std::cout << "Meta event: 0x" << std::hex << std::setw(2) << std::setfill('0') << meta_event_type << std::dec << " " << meta_event_length << std::endl;
                        if (meta_event_type == 1 || meta_event_type == 2 || meta_event_type == 3) std::cout << " " << std::string(data.begin(), data.end()) << std::endl;
                        std::cout << std::setfill(' ');
                    } else {
                        std::cout << "Unsupported system event 0x" << std::hex << std::setw(2) << std::setfill('0') << command_byte << std::dec << std::setfill(' ') << std::endl;
                        assert(false);
                    }
                } else {
                     // Channel message
                    if (command_byte & 0x80) {
                        last_message = command_byte>>4;
                        in.get();
                    }
                    assert(last_message >= 8 && last_message <= 14);                

                    static const char* const command_names[7] ={
                        "Note off", "Note on", "Key after-touch", "Control Change", "Program (patch) change", "Channel after-touch", "Pitch wheel change"
                    };
                    std::cout << std::setfill('0') << std::setw(2) << std::hex << command_byte << " " << std::dec << std::setfill(' ');
                    std::cout << std::left << std::setw(20) << command_names[last_message-8] << " " << std::right;

                    switch (last_message) {
                    case 0x8: // Note off
                    case 0x9: // Note on
                    case 0xA: // Key after-touch
                    case 0xB: // Controller change
                        {
                            const int val1 = in.get(); // key/controller
                            const int val2 = in.get(); // velocity/pressure/value
                            assert((val1&0x80) == 0);
                            assert((val2&0x80) == 0);
                            if (last_message == 8 || last_message == 9) {
                                if (val1 == 0) std::cout << "<off>";
                                else {
                                    const int octave = (val1-1)/12;
                                    const int note = (val1-1)%12;
                                    static const char* const note_names[12] = { "A-", "A#", "B-", "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#"};
                                    std::cout << note_names[note] << octave;
                                }
                                std::cout << " " << val2;
                            } else {
                                std::cout << std::hex << std::hex << std::setfill('0');
                                std::cout << "0x" << std::setw(2) << val1 << " 0x" << std::setw(2) << val2;
                                std::cout << std::dec << std::setfill(' ');
                            }
                        }
                        break;
                    case 0xC: // Program (patch) change
                    case 0xD: // Channel pressure (after-touch)
                        {
                            const int val1 = in.get();
                            assert((val1&0x80) == 0);
                            std::cout << std::hex << std::hex << std::setfill('0');
                            std::cout << "0x" << std::setw(2) << val1;
                            std::cout << std::dec << std::setfill(' ');
                        }
                        break;
                    case 0xE: // Pitch bend
                        {
                            const int val1 = in.get(); // key/controller
                            const int val2 = in.get(); // velocity/pressure/value
                            assert((val1&0x80) == 0);
                            assert((val2&0x80) == 0);
                            const int val = (val1<<7) | val2;
                            std::cout << std::hex << std::hex << std::setfill('0');
                            std::cout << "0x" << std::setw(4) << val;
                            std::cout << std::dec << std::setfill(' ');
                        }
                        break;
                    default:
                        std::cout << std::flush;
                        assert(false);
                    }

                    std::cout << std::endl;
                }

            }
        }

    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown exception caught" << std::endl;
    }
}
*/
