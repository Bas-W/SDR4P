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
#include "utils/fft.h"

namespace audio_analyzer {
    constexpr uint32_t fftFreqBinSize_default = 1024;
    constexpr uint32_t fftSampleCount_default = 2048;

    class Processor {
    public:
        Processor();
        ~Processor();

        rbuf::SharedRingBuffer<float> m_audioRingBufL;
        rbuf::SharedRingBuffer<float> m_audioRingBufR;

        rbuf::SharedRingBuffer<std::complex<float>> m_fftRingBufL;
        rbuf::SharedRingBuffer<std::complex<float>> m_fftRingBufR;

        void resizeBuffers(uint32_t size);

        bool setAudioStream(dsp::stream<dsp::stereo_t>* stream, uint32_t streamRate);
        void deselectStream();
    private:
        std::mutex m_mutex;
        std::mutex m_fftHandlerMutex;

        std::atomic<bool> m_fftHandlerShouldRun;
        std::condition_variable m_signalProcessFft;
        std::atomic<uint32_t> m_newSamples;
        std::thread m_fftHandlerThread;

        uint32_t m_fftFreqBinSize;
        uint32_t m_fftSampleRate;
        uint32_t m_fftSampleCount;

        uint32_t m_streamRate;

        uint32_t m_fftInBufSize;
        float* m_fftInBuf = nullptr;
        std::complex<float>* m_fftOutBufComplex = nullptr;

        dsp::stream<dsp::stereo_t>* m_audioStream = nullptr;
        dsp::sink::Handler<dsp::stereo_t> m_stereoSink;

        static void stereoHandler(dsp::stereo_t* data, int count, void* ctx);
        void fftHandler();
        bool processFft();

        void fftHandlerStart();
        void fftHandlerStop();
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
        std::shared_ptr<std::complex<float>> m_fftBufferL;
        std::shared_ptr<std::complex<float>> m_fftBufferR;

        std::shared_ptr<OptionList<std::string, std::string>> m_audioStreams;
        int m_audioStreamId = 0;
    };

    class AudioAnalyzer {
    public:
        AudioAnalyzer();

        void init(uint32_t fftSize = fftFreqBinSize_default);
        void doPostInit();

        void addProcessorDisplay();

        void draw();

    private:
        std::vector<std::shared_ptr<ProcessorDisplay>> m_processorDisplays;

        std::shared_ptr<OptionList<std::string, std::string>> m_audioStreams;
    };
}
