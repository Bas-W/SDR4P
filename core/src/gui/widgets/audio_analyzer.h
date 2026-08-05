#pragma once
#include <cstdint>
#include <mutex>

namespace audio_analyzer {
    constexpr uint32_t fftFreqSamples_default = 1024;
    constexpr uint32_t fftSamplerate_default = 1024;
    constexpr uint32_t fftSamplecount_default = 1024;

    class AudioAnalyzer {
    public:
        AudioAnalyzer();
        ~AudioAnalyzer();
    private:
        std::mutex m_audioRingBufMtx;
        float* m_audioRingBuf = nullptr;
        uint32_t m_audioRingBufIdx = 0;
        uint32_t m_audioRingBufSize = 0;

        void audioRingBufInit(uint32_t size);
        void audioRingBufFree();
        void audioRingBufPush(float* data, uint32_t count);
        void audioRingBufRead(float* dest, uint32_t offset, uint32_t count);
    };
}
