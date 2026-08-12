#pragma once
#include "dsp/chain.h"
#include "dsp/stream.h"
#include "dsp/types.h"
#include "dsp/buffer/frame_buffer.h"
#include "dsp/buffer/reshaper.h"
#include "dsp/correction/dc_blocker.h"
#include "dsp/multirate/power_decimator.h"
#include "dsp/routing/splitter.h"
#include "dsp/sink/handler_sink.h"
#include "utils/event.h"
#include "utils/optionlist.h"
#include <memory>
#include <vector>
#include "utils/ring_buffer.h"

#include <fftw3.h>

namespace audio_analyzer {
    constexpr uint32_t fftFreqBinSize_default = 2048;

    class Processor {
    public:

        ~Processor();

        void init(dsp::stream<dsp::complex_t>* in, double sampleRate, int decimationRatio, bool dcBlocking, int fftSize, double fftRate);

        void setSampleRate(double sampleRate);

    private:
        double m_sampleRate;
        double m_fftRate;
        int m_decimRatio;
        int m_fftSize;

        float* m_fftWindowBuf;

        fftwf_complex* m_fftInBuf;
        fftwf_complex* m_fftOutBuf;
        fftwf_plan m_fftwPlan;

        dsp::buffer::SampleFrameBuffer<dsp::complex_t> m_inBuf;

        dsp::multirate::PowerDecimator<dsp::complex_t> m_decim;
        dsp::correction::DCBlocker<dsp::complex_t> m_dcBlock;

        dsp::chain<dsp::complex_t> m_preproc;
        dsp::routing::Splitter<dsp::complex_t> m_split;
        dsp::buffer::Reshaper<dsp::complex_t> m_reshape;

        dsp::sink::Handler<dsp::complex_t> m_fftSink;
    };

    class Analyzer {
    public:
        Analyzer();
        ~Analyzer();

        void init();

        void initDisplayBuffers(size_t size);
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

        OptionList<std::string, std::string> m_audioStreams;
        EventHandler<std::string> m_onStreamRegisteredHandler;
        EventHandler<std::string> m_onStreamUnregisteredHandler;

        dsp::stream<dsp::stereo_t> m_dummyStream;
        dsp::stream<dsp::stereo_t>* m_audioStream;

        dsp::sink::Handler<dsp::stereo_t> m_audioSink;
        dsp::stream<dsp::complex_t> m_outStreamL;
        dsp::stream<dsp::complex_t> m_outStreamR;
        std::shared_ptr<Processor> m_processorL;
        std::shared_ptr<Processor> m_processorR;

        rbuf::SharedRingBuffer<float> m_displayRingBufL;
        rbuf::SharedRingBuffer<float> m_displayRingBufR;
        float* m_displayBuf = nullptr;

        static void audioHandler(dsp::stereo_t* data, int count, void* ctx);
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
