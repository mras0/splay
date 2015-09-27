#include "wavedev.h"
#include <Windows.h>
#include <cassert>
#include <stdexcept>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#pragma comment(lib, "winmm.lib")

using waveout = std::unique_ptr<HWAVEOUT__, decltype(&::waveOutClose)>;

class wavedev::impl {
public:
    explicit impl(unsigned sample_rate, callback_t callback) 
        : sample_rate_(sample_rate)
        , buffer_size_(2 * 4096)
        , callback_(callback)
        , waveout_(create_waveout())
        , exiting_(false)
        , num_buffers_to_play_(2)
        , next_buffer_(0)
        , data_(buffer_size_ * 2)
        , t_(&impl::double_buffer_thread, this) {
    }

    ~impl() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            exiting_ = true;
        }
        cv_.notify_one();
        waveOutReset(waveout_.get());
        t_.join();
    }

private:
    const unsigned              sample_rate_;
    const unsigned              buffer_size_;
    callback_t                  callback_;
    waveout                     waveout_;
    std::mutex                  mutex_;
    std::condition_variable     cv_;
    bool                        exiting_;
    int                         num_buffers_to_play_;
    int                         next_buffer_;
    std::vector<short>          data_;
    WAVEHDR                     hdr_[2];
    std::thread                 t_;

    waveout create_waveout() {
        HWAVEOUT hwo = nullptr;
        WAVEFORMATEX wfx ={0,};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = 2;
        wfx.nSamplesPerSec = sample_rate_;
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        wfx.cbSize = sizeof(wfx);

        MMRESULT ret = waveOutOpen(&hwo, WAVE_MAPPER, &wfx, reinterpret_cast<DWORD_PTR>(s_callback), reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
        if (ret != MMSYSERR_NOERROR) {
            throw std::runtime_error("waveOutOpen failed: " + std::to_string(ret));
        }
        return {hwo, &::waveOutClose};
    }

    static void CALLBACK s_callback(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
        assert(dwInstance);
        auto& instance = *reinterpret_cast<impl*>(dwInstance);
        if (uMsg == MM_WOM_OPEN || uMsg == MM_WOM_CLOSE) {
            return;
        }
        assert(instance.waveout_.get() == hwo);
        if (uMsg == MM_WOM_DONE) {
            {
                std::lock_guard<std::mutex> lock(instance.mutex_);
                assert(instance.num_buffers_to_play_ >= 0 && instance.num_buffers_to_play_ < 2);
                instance.num_buffers_to_play_++;
                if (instance.exiting_) {
                    return;
                }
            }
            instance.cv_.notify_one();
        }
        (void)hwo;
        (void)dwParam1;
        (void)dwParam2;
    }

    void double_buffer_thread() {
        assert(waveout_.get());
        for (;;) {
            int buffer;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return exiting_ || num_buffers_to_play_; });
                if (exiting_) break;
                assert(num_buffers_to_play_ >= 1 && num_buffers_to_play_ <= 2);
                buffer = next_buffer_;
                num_buffers_to_play_--;
                next_buffer_ = !next_buffer_;
            }
            callback_(&data_[buffer * buffer_size_], buffer_size_);
            memset(&hdr_[buffer], 0, sizeof(WAVEHDR));
            hdr_[buffer].lpData  = (LPSTR)&data_[buffer * buffer_size_];
            hdr_[buffer].dwBufferLength = buffer_size_ * 2;
            auto ret = waveOutPrepareHeader(waveout_.get(), &hdr_[buffer], sizeof(WAVEHDR));
            assert(ret == MMSYSERR_NOERROR);
            ret = waveOutWrite(waveout_.get(), &hdr_[buffer], sizeof(WAVEHDR));
            assert(ret == MMSYSERR_NOERROR);
        }
    }
};

wavedev::wavedev(unsigned sample_rate, callback_t callback)
    : impl_(new impl(sample_rate, callback))
{
}

wavedev::~wavedev() = default;