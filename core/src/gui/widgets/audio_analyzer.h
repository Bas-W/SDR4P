#pragma once
#include "dsp/chain.h"
#include "dsp/stream.h"
#include "dsp/types.h"
#include "dsp/buffer/frame_buffer.h"
#include "dsp/buffer/reshaper.h"
#include "dsp/correction/dc_blocker.h"
#include "dsp/multirate/power_decimator.h"
#include "dsp/sink/handler_sink.h"
#include "utils/event.h"
#include "utils/optionlist.h"
#include <memory>
#include <vector>
#include "utils/ring_buffer.h"
#include <mutex>

#include <fftw3.h>

namespace audio_analyzer {
    constexpr uint32_t fftFreqBinSize_default = 2048;
    constexpr uint32_t fftFreqRate_default = 15;
    constexpr uint32_t fftWaterfallBinCount_default = 256;

    class Processor {
    public:

        ~Processor();

        void init(dsp::stream<dsp::complex_t>* in, double sampleRate, int decimationRatio, bool dcBlocking, int fftSize, double fftRate, void (*fftHandler)(float* data, int count, void* ctx), void* fftHandlerCtx);

        void start();
        void stop();

        void setSampleRate(double sampleRate);

        void readLatestFft(float* dest, size_t count);

        int getFFTSize() const { return m_fftSize; }
        double getEffectiveSampleRate() const { return m_effectiveSR; }

    private:
        double m_sampleRate = 0.0;
        double m_effectiveSR = 0.0;
        double m_fftRate = 0.0;
        int m_decimRatio = 0;
        int m_fftSize = 0;
        int m_nzFFTSize = 0;

        float* m_fftWindowBuf = nullptr;

        fftwf_complex* m_fftInBuf = nullptr;
        fftwf_complex* m_fftOutBuf = nullptr;
        fftwf_plan m_fftwPlan = nullptr;

        std::mutex m_fftDisplayMtx;
        std::vector<float> m_latestFFT;

        dsp::buffer::SampleFrameBuffer<dsp::complex_t> m_inBuf;

        dsp::multirate::PowerDecimator<dsp::complex_t> m_decim;
        dsp::correction::DCBlocker<dsp::complex_t> m_dcBlock;

        dsp::stream<dsp::complex_t> fftIn;
        dsp::chain<dsp::complex_t> m_preproc;
        dsp::buffer::Reshaper<dsp::complex_t> m_reshape;

        dsp::sink::Handler<dsp::complex_t> m_fftSink;

        void (*m_fftHandler)(float* data, int count, void* ctx) = nullptr;
        void* m_fftHandlerCtx = nullptr;

        bool m_init = false;

        static void handler(dsp::complex_t* data, int count, void* ctx);
        void updateFFTPath();

        static inline void genReshapeParams(double sampleRate, int size, double rate, int& skip, int& nzSampCount) {
            int fftInterval = round(sampleRate / rate);
            nzSampCount = std::min<int>(fftInterval, size);
            skip = fftInterval - nzSampCount;
        }
    };

    class Analyzer {
    public:
        Analyzer();
        ~Analyzer();

        void init();

        void initDisplayBuffers(size_t waveformBufSize, size_t waterfallBinCount = fftWaterfallBinCount_default);
        void freeDisplayBuffers();

        void start();
        void stop();

        void setAudioStream(std::string streamName);
        void unsetAudioStream();

        void setMono(bool mono);
        bool isMono();

        void draw();
    private:
        std::mutex m_mutex;
        std::mutex m_displayBufMutex;

        bool m_mono = true;
        size_t m_displayBufSize = 0;
        int m_audioStreamId = 0;
        std::string m_audioStreamName;
        uint64_t m_sampleRate = 0;

        int m_fftSize = fftFreqBinSize_default;
        size_t m_waterfallBinCount = fftWaterfallBinCount_default;

        OptionList<std::string, std::string> m_audioStreams;
        EventHandler<std::string> m_onStreamRegisteredHandler;
        EventHandler<std::string> m_onStreamUnregisteredHandler;

        dsp::stream<dsp::stereo_t> m_dummyStream;
        dsp::stream<dsp::stereo_t>* m_audioStream;

        dsp::sink::Handler<dsp::stereo_t> m_audioSink;
        dsp::stream<dsp::complex_t> m_outStreamL;
        dsp::stream<dsp::complex_t> m_outStreamR;
        std::unique_ptr<Processor> m_processorL;
        std::unique_ptr<Processor> m_processorR;

        rbuf::SharedRingBuffer<float> m_displayRingBufL;
        rbuf::SharedRingBuffer<float> m_displayRingBufR;
        rbuf::SharedRingBuffer<float> m_waterfallRingBufL;
        rbuf::SharedRingBuffer<float> m_waterfallRingBufR;

        float* m_displayBuf = nullptr;
        float* m_fftDisplayBuf = nullptr;
        float* m_waterfallDisplayBuf = nullptr;

        static void audioHandler(dsp::stereo_t* data, int count, void* ctx);

        static void fftHandlerL(float* data, int count, void* ctx);
        static void fftHandlerR(float* data, int count, void* ctx);

        static void streamRegisteredHandler(std::string name, void* ctx);
        static void streamUnregisteredHandler(std::string name, void* ctx);
    };

    class Manager {
    public:
        /*
        Manager();
        ~Manager();

        void init();
        */
        void doPostInit();

        void addAnalyzer();

        void draw();

    private:
        std::vector<std::shared_ptr<Analyzer>> m_analyzers;
        std::shared_ptr<OptionList<std::string, std::string>> m_audioStreams;
    };
}
