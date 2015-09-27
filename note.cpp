#include "note.h"
#include <assert.h>
#include <math.h>

namespace splay {

static_assert(piano_key::GS4 == piano_key::A_4 + 11, "");

float note_difference_to_scale(int note_diff)
{
    // To go up a semi-tone multiply the frequency by pow(2,1./12) ~1.06
    return pow(2.0f, static_cast<float>(note_diff) / notes_per_octave);
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

piano_key key_to_note(int vk) {
    constexpr piano_key offset = piano_key::A_4;
    if (vk == 'Z') return offset - 9; // C
    if (vk == 'S') return offset - 8; // C#
    if (vk == 'X') return offset - 7; // D
    if (vk == 'D') return offset - 6; // D#
    if (vk == 'C') return offset - 5; // E
    if (vk == 'V') return offset - 4; // F
    if (vk == 'G') return offset - 3; // F#
    if (vk == 'B') return offset - 2; // G
    if (vk == 'H') return offset - 1; // G#
    if (vk == 'N') return offset - 0; // A
    if (vk == 'J') return offset + 1; // A#
    if (vk == 'M') return offset + 2; // B
    return piano_key::OFF;
}

} // namespace splay
