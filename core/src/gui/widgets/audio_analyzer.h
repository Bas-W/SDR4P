#pragma once
#include "dsp/stream.h"
#include "dsp/types.h"
#include "dsp/sink/handler_sink.h"
#include "utils/optionlist.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>
#include "utils/ring_buffer.h"

namespace audio_analyzer {
    constexpr uint32_t fftFreqSamples_default = 1024;
    constexpr uint32_t fftSamplerate_default = 1024;
    constexpr uint32_t fftSamplecount_default = 1024;

    static void stereoHandler(dsp::stereo_t* data, int count, void* ctx);

    class Processor {
    public:
        Processor();
        ~Processor();

        void resizeBuffers(uint32_t size);

        rbuf::SharedRingBuffer m_ringBufL;
        rbuf::SharedRingBuffer m_ringBufR;

        bool setAudioStream(dsp::stream<dsp::stereo_t>* stream);
        void deselectStream();
    private:
        std::recursive_mutex m_recMtx;

        dsp::stream<dsp::stereo_t>* m_audioStream = nullptr;
        dsp::sink::Handler<dsp::stereo_t> m_stereoSink;
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

        void setAudioStreams(std::shared_ptr<OptionList<std::string, std::string>> audioStreams);

        void draw();

        void resizeBuffers(uint32_t size);
        void updateBuffers();
        std::shared_ptr<const float> getBufferLeft();
        std::shared_ptr<const float> getBufferRight();
        const uint32_t bufferSize();

    private:
        uint32_t m_bufferSize;

        std::shared_ptr<Processor> m_processor;
        std::shared_ptr<float> m_bufferL;
        std::shared_ptr<float> m_bufferR;

        std::shared_ptr<OptionList<std::string, std::string>> m_audioStreams;
        int m_audioStreamId = 0;
    };

    class AudioAnalyzer {
    public:
        AudioAnalyzer();

        void doPostInit();

        void addProcessorDisplay();

        void draw();

    private:
        std::vector<std::shared_ptr<ProcessorDisplay>> m_processorDisplays;

        std::shared_ptr<OptionList<std::string, std::string>> m_audioStreams;
    };
}
