#ifndef WAVEDEV_H_INCLUDED
#define WAVEDEV_H_INCLUDED

#include <memory>
#include <functional>

class wavedev {
public:
    using callback_t = std::function<void(short*, size_t)>;

    explicit wavedev(unsigned sample_rate, callback_t callback);
    ~wavedev();

    wavedev(const wavedev&) = delete;
    wavedev& operator=(const wavedev&) = delete;

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif