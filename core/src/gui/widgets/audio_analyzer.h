#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace audio_analyzer {
    constexpr uint32_t fftFreqSamples_default = 1024;
    constexpr uint32_t fftSamplerate_default = 1024;
    constexpr uint32_t fftSamplecount_default = 1024;

    class Processor {
    public:
        ~Processor();

        void audioRingBufInit(uint32_t size);
        void audioRingBufFree();
        void audioRingBufPush(float* data, uint32_t count);
        void audioRingBufRead(float* dest, uint32_t offset, uint32_t count);

    private:
        std::mutex m_audioRingBufMtx;
        std::shared_ptr<float> m_audioRingBuf;
        uint32_t m_audioRingBufIdx = 0;
        uint32_t m_audioRingBufSize = 0;
        bool m_audioRingBufWrapped = false;
    };

    class ProcessorWorker {
    public:
        ~ProcessorWorker();

        void init(std::shared_ptr<Processor> processor);

        void start();
        void stop(bool joinThread = true);

    private:
        std::atomic<bool> m_shouldRun = false;
        std::thread m_thread;
        std::shared_ptr<Processor> m_processor;

        void process();
    };

    class ProcessorDisplay {
    public:
        ProcessorDisplay(std::shared_ptr<Processor> processor, uint32_t size);

        void resizeBuffer(uint32_t size);
        void updateBuffer();
        std::shared_ptr<const float> getBuffer();
        const uint32_t bufferSize();

    private:
        uint32_t m_bufferSize;

        std::shared_ptr<Processor> m_processor;
        std::shared_ptr<float> m_buffer;
    };

    class AudioAnalyzer {
    public:

        void addProcessorDisplay(std::shared_ptr<ProcessorDisplay> processorDisplay);

        void draw();

    private:
        std::vector<std::shared_ptr<ProcessorDisplay>> m_processorDisplays;
    };
}
