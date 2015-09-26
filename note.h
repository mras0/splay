#ifndef SPLAY_NOTE_H
#define SPLAY_NOTE_H

#include <string>

namespace splay {

// https://en.wikipedia.org/wiki/Piano_key_frequencies
// A-4 (440 Hz) is the 49th key on an idealized keyboard
constexpr int notes_per_octave = 12;

enum class piano_key : unsigned char {
    OFF = 0,
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

float note_difference_to_scale(int note_diff);

constexpr bool piano_key_valid(piano_key n)
{
    return n >= piano_key::A_0 && n <= piano_key::C_8;
}

float piano_key_to_freq(piano_key n);

std::string piano_key_to_string(piano_key n);

} // namespace splay

#endif
