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

} // namespace splay
