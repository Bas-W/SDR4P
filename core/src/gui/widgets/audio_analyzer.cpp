#include "audio_analyzer.h"

#include "imgui.h"
#include "implot.h"
#include "signal_path/signal_path.h"

namespace audio_analyzer {
    Processor::~Processor() {
        return;
    }

    void Processor::init(dsp::stream<dsp::complex_t>* in, double sampleRate, int decimationRatio, bool dcBlocking, int fftSize, double fftRate) {
        return;
    }

    void Processor::setSampleRate(double sampleRate) {
        return;
    }

    Analyzer::Analyzer() {
        m_audioSink.init(&m_dummyStream, audioHandler, this);
    }

    Analyzer::~Analyzer() {
        stop();
        freeDisplayBuffers();
    }

    void Analyzer::init() {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_audioStreams.clear();
        auto names = sigpath::sinkManager.getStreamNames();
        for (const auto& name : names) {
            m_audioStreams.define(name, name, name);
        }

        m_onStreamRegisteredHandler.ctx = this;
        m_onStreamRegisteredHandler.handler = streamRegisteredHandler;
        sigpath::sinkManager.onStreamRegistered.bindHandler(&m_onStreamRegisteredHandler);
        m_onStreamUnregisteredHandler.ctx = this;
        m_onStreamUnregisteredHandler.handler = streamUnregisteredHandler;
        sigpath::sinkManager.onStreamUnregister.bindHandler(&m_onStreamUnregisteredHandler);
    }

    void Analyzer::initDisplayBuffers(size_t size) {
        std::lock_guard<std::mutex> lockDisp(m_displayBufMtx);

        m_displayBufSize = size;

        if (m_displayBufL) {
            free(m_displayBufL);
        }
        m_displayBufL = static_cast<float*>(malloc(size * sizeof(float)));

        if (!m_mono) {
            if (m_displayBufR) {
                free(m_displayBufR);
            }
            m_displayBufR = static_cast<float*>(malloc(size * sizeof(float)));
        }
    }

    void Analyzer::freeDisplayBuffers() {
        std::lock_guard<std::mutex> lockDisp(m_displayBufMtx);

        if (m_displayBufL) free(m_displayBufL);
        if (m_displayBufR) free(m_displayBufR);

        m_displayBufSize = 0;
    }

    void Analyzer::start() {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_audioSink.start();
    }

    void Analyzer::stop() {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_audioSink.stop();
    }

    /// Doesn't lock mutex automatically
    void Analyzer::setAudioStream(std::string streamName) {
        //@todo: Add ability to update settings of existing processor
        m_audioSink.tempStop();

        unsetAudioStream();

        if (m_audioStreams.empty()) {
            m_audioStreamName.clear();
            return;
        }

        if (!m_audioStreams.keyExists(streamName)) {
            m_audioStreamName.clear();
            return;
        }

        m_audioStream = sigpath::sinkManager.bindStream(streamName);
        if (!m_audioStream) return;

        m_audioStreamName = streamName;
        m_audioStreamId = m_audioStreams.keyId(streamName);
        m_sampleRate = sigpath::sinkManager.getStreamSampleRate(streamName);

        if (!m_processorL) {
            m_processorL = std::make_unique<Processor>();
            m_processorL->init(&m_outStreamL, m_sampleRate, 1, false, fftFreqBinSize_default, 10);
        }
        else {
            m_processorL->setSampleRate(m_sampleRate);
        }

        if (!m_mono) {
            if (!m_processorR) {
                m_processorR = std::make_unique<Processor>();
                m_processorR->init(&m_outStreamR, m_sampleRate, 1, false, fftFreqBinSize_default, 10);
            }
            else {
                m_processorR->setSampleRate(m_sampleRate);
            }
        }

        m_audioSink.setInput(m_audioStream);
        m_audioSink.tempStart();
    }

    /// Doesn't lock mutex automatically
    void Analyzer::unsetAudioStream() {
        if (m_audioStreamName.empty() || !m_audioStream) {
            return;
        }
        sigpath::sinkManager.unbindStream(m_audioStreamName, m_audioStream);
        m_audioStreamName.clear();
        m_audioStream = nullptr;
    }

    size_t Analyzer::displayBufSize() {
        std::lock_guard<std::mutex> lockDisp(m_displayBufMtx);
        return m_displayBufSize;
    }

    void Analyzer::setMono(bool mono) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_mono = mono;
    }

    bool Analyzer::isMono() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_mono;
    }

    bool Analyzer::readDisplayBufL(float* dest, size_t count) {
        ZoneScoped;
        std::lock_guard<std::mutex> lockDisp(m_displayBufMtx);

        if (count > m_displayBufSize) return false;

        if (m_displayBufL == nullptr) return false;

        std::memcpy(dest, m_displayBufL, count * sizeof(float));

        return true;
    }

    bool Analyzer::readDisplayBufR(float* dest, size_t count) {
        ZoneScoped;
        std::lock_guard<std::mutex> lockDisp(m_displayBufMtx);

        if (count > m_displayBufSize) return false;

        if (m_displayBufR == nullptr) return false;

        std::memcpy(dest, m_displayBufR, count * sizeof(float));

        return true;
    }

    void Analyzer::draw() {
        ZoneScoped;
        std::unique_lock<std::mutex> lock(m_mutex);

        bool isMono = m_mono;
        size_t displayBufSize = m_displayBufSize;

        if (ImGui::Combo(("##_recorder_stream_" + m_audioStreamName).c_str(), &m_audioStreamId, m_audioStreams.txt)) {
            setAudioStream(m_audioStreams.value(m_audioStreamId));
        }

        lock.unlock();

        if (ImGui::BeginChild("##analyzer_disp")) {
            if (displayBufSize > 0) {
                if (ImPlot::BeginPlot(isMono ? "Waveform" : "Waveform Left")) {

                    float* data = static_cast<float*>(malloc(displayBufSize * sizeof(float)));

                    readDisplayBufL(data, displayBufSize);

                    ImPlot::PlotLine("##analyzer_plot_waveform", data, displayBufSize);

                    ImPlot::EndPlot();
                }

                if (!isMono) {
                    if (ImPlot::BeginPlot("Waveform Right")) {

                        float* data = static_cast<float*>(malloc(displayBufSize * sizeof(float)));

                        readDisplayBufR(data, displayBufSize);

                        ImPlot::PlotLine("##analyzer_plot_waveform", data, displayBufSize);

                        ImPlot::EndPlot();
                    }
                }
            }
            else {
                ImGui::Text("no data");
            }
        }
        ImGui::EndChild();
    }

    void Analyzer::audioHandler(dsp::stereo_t* data, int count, void* ctx) {
        ZoneScoped;
        Analyzer* _this = static_cast<Analyzer*>(ctx);

        std::lock_guard<std::mutex> lock(_this->m_mutex);

        if (_this->m_mono && _this->m_processorL) {
            size_t countToCpy = 0;
            dsp::complex_t* out = _this->m_outStreamL.writeBuf;

            std::lock_guard<std::mutex> lockDisp(_this->m_displayBufMtx);

            if (_this->m_displayBufL && count < _this->m_displayBufSize) {
                countToCpy = _this->m_displayBufSize - count;

                float* temp = static_cast<float*>(malloc(countToCpy * sizeof(float)));

                std::memcpy(temp, _this->m_displayBufL, countToCpy * sizeof(float));
                std::memcpy(_this->m_displayBufL + count, temp,countToCpy * sizeof(float));

                free(temp);
            }

            for (int i = 0; i < count; i++) {
                float re = (data[i].l + data[i].r) / 2.0f;
                out[i] = dsp::complex_t{ re, 0.0f };
                if (_this->m_displayBufL && i < _this->m_displayBufSize) {
                    _this->m_displayBufL[count - i - 1] = re;
                }
            }

            //if (!_this->m_outStreamL.swap(count)) flog::error("audio_analyzer failed to write data to outStreamL");

            return;
        }

        if (_this->m_processorL && _this->m_processorR) {
            size_t countToCpy = 0;
            dsp::complex_t* outL = _this->m_outStreamL.writeBuf;
            dsp::complex_t* outR = _this->m_outStreamR.writeBuf;

            std::lock_guard<std::mutex> lockDisp(_this->m_displayBufMtx);

            if (_this->m_displayBufL && _this->m_displayBufR && count < _this->m_displayBufSize) {
                countToCpy = _this->m_displayBufSize - count;

                float* temp = static_cast<float*>(malloc(countToCpy * sizeof(float)));

                std::memcpy(temp, _this->m_displayBufL, countToCpy * sizeof(float));
                std::memcpy(_this->m_displayBufL + count, temp,countToCpy * sizeof(float));

                std::memcpy(temp, _this->m_displayBufR, countToCpy);
                std::memcpy(_this->m_displayBufR + count, temp,countToCpy * sizeof(float));

                free(temp);
            }

            for (int i = 0; i < count; i++) {
                outL[i] = dsp::complex_t{ data[i].l, 0.0f };
                outR[i] = dsp::complex_t{ data[i].r, 0.0f };

                if (_this->m_displayBufL && _this->m_displayBufR && i < _this->m_displayBufSize) {
                    _this->m_displayBufL[count - i - 1] = data[i].l;
                    _this->m_displayBufR[count - i - 1] = data[i].r;
                }
            }

            //if (!_this->m_outStreamL.swap(count)) flog::error("audio_analyzer failed to write data to outStreamL");
            //if (!_this->m_outStreamR.swap(count)) flog::error("audio_analyzer failed to write data to outStreamR");
            return;
        }

        flog::error("audio_analyzer failed; processors are nullptr");
    }

    void Analyzer::streamRegisteredHandler(std::string name, void* ctx) {
        Analyzer* _this = static_cast<Analyzer*>(ctx);

        // Add new stream to the list
        _this->m_audioStreams.define(name, name, name);

        // If no stream is selected, select new stream. If not, update the menu ID.
        if (_this->m_audioStreamName.empty()) {
            _this->setAudioStream(name);
        }
        else {
            _this->m_audioStreamId = _this->m_audioStreams.keyId(_this->m_audioStreamName);
        }
    }

    void Analyzer::streamUnregisteredHandler(std::string name, void* ctx) {
        Analyzer* _this = static_cast<Analyzer*>(ctx);

        _this->m_audioStreams.undefineKey(name);

        // If the stream is in use, deselect it and reselect default. Otherwise, update ID.
        if (_this->m_audioStreamName == name) {
            _this->setAudioStream("");
        }
        else {
            _this->m_audioStreamId = _this->m_audioStreams.keyId(_this->m_audioStreamName);
        }
    }

    void Manager::doPostInit() {
        return;
    }

    void Manager::addAnalyzer() {
        std::shared_ptr<Analyzer> analyzer = std::make_shared<Analyzer>();
        analyzer->init();
        analyzer->initDisplayBuffers(240000);
        analyzer->start();
        m_analyzers.push_back(analyzer);
    }

    void Manager::draw() {
        ZoneScoped;
        if (ImGui::BeginChild("Audio Analyzer")) {

            if (ImGui::Button("Add Analyzer")) {
                addAnalyzer();
            }

            for (int i = 0; i < m_analyzers.size(); i++) {
                m_analyzers.at(i)->draw();
            }

            ImGui::EndChild();
        }
    }
}