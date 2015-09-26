#ifndef SPLAY_MIDI_H
#define SPLAY_MIDI_H

#include <iosfwd>
#include <memory>

namespace splay { namespace midi {

class player {
public:
    explicit player(std::istream& in);
    ~player();

private:
    class impl;
    std::unique_ptr<impl> impl_;
};


} } // namespace splay::midi

#endif
