#include "audio_analyzer.h"

#include "imgui.h"
#include "implot.h"
#include "signal_path/signal_path.h"

namespace audio_analyzer {
    Processor::~Processor() {
        if (!m_init) { return; }

        stop();

        if (m_fftWindowBuf) {
            dsp::buffer::free(m_fftWindowBuf);
            m_fftWindowBuf = nullptr;
        }

        if (m_fftwPlan) {
            fftwf_destroy_plan(m_fftwPlan);
            m_fftwPlan = nullptr;
        }

        if (m_fftInBuf) {
            fftwf_free(m_fftInBuf);
            m_fftInBuf = nullptr;
        }

        if (m_fftOutBuf) {
            fftwf_free(m_fftOutBuf);
            m_fftOutBuf = nullptr;
        }

        m_init = false;
    }

    void Processor::init(dsp::stream<dsp::complex_t>* in, double sampleRate, int decimationRatio, bool dcBlocking, int fftSize, double fftRate, void (*fftHandler)(float* data, int count, void* ctx), void* fftHandlerCtx) {
        m_sampleRate = sampleRate;
        m_decimRatio = decimationRatio;
        m_fftSize = fftSize;
        m_fftRate = fftRate;
        m_fftHandler = fftHandler;
        m_fftHandlerCtx = fftHandlerCtx;

        m_effectiveSR = m_sampleRate / m_decimRatio;

        m_inBuf.init(in);
        m_decim.init(nullptr, m_decimRatio);
        m_dcBlock.init(nullptr, 50.0 / m_effectiveSR);

        m_preproc.init(&m_inBuf.out);
        m_preproc.addBlock(&m_decim, m_decimRatio > 1);
        m_preproc.addBlock(&m_dcBlock, dcBlocking);

        int skip;
        genReshapeParams(m_effectiveSR, m_fftSize, m_fftRate, skip, m_nzFFTSize);

        m_reshape.init(m_preproc.out, m_fftSize, skip);
        m_fftSink.init(&m_reshape.out, handler, this);

        m_fftWindowBuf = dsp::buffer::alloc<float>(m_nzFFTSize);
        for (int i = 0; i < m_nzFFTSize; i++) {
            m_fftWindowBuf[i] = dsp::window::nuttall(i, m_nzFFTSize);
        }

        m_fftInBuf = static_cast<fftwf_complex*>(fftwf_malloc(m_fftSize * sizeof(fftwf_complex)));
        m_fftOutBuf = static_cast<fftwf_complex*>(fftwf_malloc(m_fftSize * sizeof(fftwf_complex)));
        m_fftwPlan = fftwf_plan_dft_1d(m_fftSize, m_fftInBuf, m_fftOutBuf, FFTW_FORWARD, FFTW_ESTIMATE);

        dsp::buffer::clear(m_fftInBuf, m_fftSize);

        m_init = true;
    }

    void Processor::start() {
        if (!m_init) { return; }

        m_inBuf.start();
        m_preproc.start();
        m_reshape.start();
        m_fftSink.start();
    }

    void Processor::stop() {
        if (!m_init) { return; }

        m_fftSink.stop();
        m_reshape.stop();
        m_preproc.stop();
        m_inBuf.stop();
    }

    void Processor::setSampleRate(double sampleRate) {
        if (!m_init) {
            m_sampleRate = sampleRate;
            return;
        }

        m_dcBlock.tempStop();
        m_reshape.tempStop();
        m_fftSink.tempStop();

        m_sampleRate = sampleRate;
        m_effectiveSR = m_sampleRate / m_decimRatio;
        m_dcBlock.setRate(50.0 / m_effectiveSR);

        updateFFTPath();

        m_dcBlock.tempStart();
        m_reshape.tempStart();
        m_fftSink.tempStart();
    }

    void Processor::readLatestFft(float* dest, size_t count) {
        if (!dest || count == 0) { return; }

        std::lock_guard<std::mutex> lock(m_fftDisplayMtx);

        size_t copyCount = std::min<size_t>(count, m_latestFFT.size());
        std::copy(m_latestFFT.begin(), m_latestFFT.begin() + copyCount, dest);
    }

    void Processor::handler(dsp::complex_t* data, int count, void* ctx) {
        Processor* _this = static_cast<Processor*>(ctx);
        if (!_this || !_this->m_init) { return; }

        dsp::buffer::clear(_this->m_fftInBuf, _this->m_fftSize);

        int copyCount = std::min<int>(count, _this->m_nzFFTSize);

        for (int i = 0; i < copyCount; i++) {
            _this->m_fftInBuf[i][0] = data[i].re * _this->m_fftWindowBuf[i];
            _this->m_fftInBuf[i][1] = data[i].im * _this->m_fftWindowBuf[i];
        }

        fftwf_execute(_this->m_fftwPlan);

        const int usefulBins = _this->m_fftSize / 2;

        std::lock_guard<std::mutex> lock(_this->m_fftDisplayMtx);

        if (_this->m_latestFFT.size() != usefulBins) {
            _this->m_latestFFT.resize(usefulBins);
        }

        for (int i = 0; i < usefulBins; i++) {
            float re = _this->m_fftOutBuf[i][0];
            float im = _this->m_fftOutBuf[i][1];
            float mag = sqrtf((re * re) + (im * im)) / static_cast<float>(_this->m_fftSize);
            _this->m_latestFFT[i] = 20.0f * log10f(mag + 1e-12f);
        }

        _this->m_fftHandler(_this->m_latestFFT.data(), _this->m_latestFFT.size(), _this->m_fftHandlerCtx);
    }

    void Processor::updateFFTPath() {
        int skip;
        genReshapeParams(m_effectiveSR, m_fftSize, m_fftRate, skip, m_nzFFTSize);

        m_reshape.setKeep(m_nzFFTSize);
        m_reshape.setSkip(skip);

        if (m_fftWindowBuf) {
            dsp::buffer::free(m_fftWindowBuf);
        }

        m_fftWindowBuf = dsp::buffer::alloc<float>(m_nzFFTSize);
        for (int i = 0; i < m_nzFFTSize; i++) {
            m_fftWindowBuf[i] = dsp::window::nuttall(i, m_nzFFTSize);
        }

        if (m_fftwPlan) {
            fftwf_destroy_plan(m_fftwPlan);
        }

        if (m_fftInBuf) {
            fftwf_free(m_fftInBuf);
        }

        if (m_fftOutBuf) {
            fftwf_free(m_fftOutBuf);
        }

        m_fftInBuf = static_cast<fftwf_complex*>(fftwf_malloc(m_fftSize * sizeof(fftwf_complex)));
        m_fftOutBuf = static_cast<fftwf_complex*>(fftwf_malloc(m_fftSize * sizeof(fftwf_complex)));
        m_fftwPlan = fftwf_plan_dft_1d(m_fftSize, m_fftInBuf, m_fftOutBuf, FFTW_FORWARD, FFTW_ESTIMATE);

        dsp::buffer::clear(m_fftInBuf, m_fftSize);

        {
            std::lock_guard<std::mutex> lock(m_fftDisplayMtx);
            m_latestFFT.assign(m_fftSize / 2, -120.0f);
        }
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

        if (!names.empty()) setAudioStream(names.at(0));
    }

    void Analyzer::initDisplayBuffers(size_t waveformBufSize, size_t waterfallBinCount) {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::lock_guard<std::mutex> lockDispBuf(m_displayBufMutex);

        m_displayBufSize = waveformBufSize;
        m_waterfallBinCount = waterfallBinCount;

        m_displayRingBufL.init(m_displayBufSize);

        if (!m_mono) {
            m_displayRingBufR.init(m_displayBufSize);
        }
        else {
            m_displayRingBufR.freeBuf();
        }

        m_waterfallRingBufL.init(m_waterfallBinCount * m_fftSize / 2);

        if (!m_mono) {
            m_waterfallRingBufR.init(m_waterfallBinCount * m_fftSize / 2);
        }
        else {
            m_waterfallRingBufR.freeBuf();
        }

        if (m_displayBuf) free(m_displayBuf);
        m_displayBuf = static_cast<float*>(malloc(m_displayBufSize * sizeof(float)));

        if (m_fftDisplayBuf) free(m_fftDisplayBuf);
        m_fftDisplayBuf = static_cast<float*>(malloc((m_fftSize / 2) * sizeof(float)));

        if (m_waterfallDisplayBuf) free(m_waterfallDisplayBuf);
        m_waterfallDisplayBuf = static_cast<float*>(malloc(m_waterfallBinCount * m_fftSize / 2 * sizeof(float)));
    }

    void Analyzer::freeDisplayBuffers() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::lock_guard<std::mutex> lockDispBuf(m_displayBufMutex);

        m_displayBufSize = 0;

        m_displayRingBufL.freeBuf();
        m_displayRingBufR.freeBuf();

        m_waterfallRingBufL.freeBuf();
        m_waterfallRingBufR.freeBuf();

        if (m_displayBuf) {
            free(m_displayBuf);
            m_displayBuf = nullptr;
        }

        if (m_fftDisplayBuf) {
            free(m_fftDisplayBuf);
            m_fftDisplayBuf = nullptr;
        }

        if (m_waterfallDisplayBuf) {
            free(m_waterfallDisplayBuf);
            m_waterfallDisplayBuf = nullptr;
        }
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
            m_processorL->init(&m_outStreamL, m_sampleRate, 1, false, fftFreqBinSize_default, fftFreqRate_default, fftHandlerL, this);
            m_processorL->start();
        }
        else {
            m_processorL->setSampleRate(m_sampleRate);
            m_processorL->start();
        }

        if (!m_mono) {
            if (!m_processorR) {
                m_processorR = std::make_unique<Processor>();
                m_processorR->init(&m_outStreamR, m_sampleRate, 1, false, fftFreqBinSize_default, fftFreqRate_default, fftHandlerR, this);
                m_processorR->start();
            }
            else {
                m_processorR->setSampleRate(m_sampleRate);
                m_processorR->start();
            }
        }

        m_audioSink.setInput(m_audioStream);
        m_audioSink.tempStart();
    }

    /// Doesn't lock mutex automatically
    void Analyzer::unsetAudioStream() {
        if (m_processorL) m_processorL->stop();
        if (m_processorR) m_processorR->stop();
        if (m_audioStreamName.empty() || !m_audioStream) {
            return;
        }
        sigpath::sinkManager.unbindStream(m_audioStreamName, m_audioStream);
        m_audioStreamName.clear();
        m_audioStream = nullptr;
    }

    void Analyzer::setMono(bool mono) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_mono = mono;
    }

    bool Analyzer::isMono() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_mono;
    }

    void Analyzer::draw() {
        ZoneScoped;
        std::unique_lock<std::mutex> lock(m_mutex);

        bool isMono = m_mono;
        DisplayMode displayMode = m_displayMode;
        bool dispModeChanged = false;
        size_t displayBufSize = m_displayBufSize;
        size_t waterfallFreqBinCount = m_waterfallBinCount;
        int fftSize = m_fftSize;
        uint64_t sampleRate = m_sampleRate;

        ImGui::SetNextItemWidth(150 * style::uiScale);
        if (ImGui::Combo(("Source##analyzer_stream_" + m_audioStreamName).c_str(), &m_audioStreamId, m_audioStreams.txt)) {
            setAudioStream(m_audioStreams.value(m_audioStreamId));
        }

        ImGui::SameLine();

        ImGui::SetNextItemWidth(150 * style::uiScale);
        if (ImGui::Combo("Freq. Axis Scaling##analyzer_dispMode", reinterpret_cast<int*>(&displayMode), DisplayMode_str)) {
            m_displayMode = displayMode;
            dispModeChanged = true;
        }

        double binHz = static_cast<double>(sampleRate) / static_cast<double>(fftSize);
        int usefulBins = fftSize / 2;

        lock.unlock();

        if (ImGui::BeginChild("##analyzer_disp")) {
            if (displayBufSize > 0) {
                std::lock_guard<std::mutex> lockDispBuf(m_displayBufMutex);

                if (ImGui::BeginTable("##analyzer_table", 2)) {

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    ImVec2 spaceAvail = ImGui::GetContentRegionAvail();

                    static float ratiosWav[] = { 1, 1 };
                    if (ImPlot::BeginSubplots("Waveform##analyzer_waveform_plots", isMono ? 1 : 2, 1, ImVec2(-1.0f, spaceAvail.y - ImGui::GetStyle().ItemSpacing.y),
                                              ImPlotSubplotFlags_ColMajor | ImPlotSubplotFlags_LinkAllX | ImPlotSubplotFlags_LinkAllY | ImPlotSubplotFlags_NoLegend, isMono ? 0 : ratiosWav)) {
                        if (ImPlot::BeginPlot(isMono ? "##analyzer_plot_waveform_l" : "Left##analyzer_plot_waveform_l", ImVec2(-1.0f, 0))) {

                            ImPlot::SetupAxis(ImAxis_X1, NULL, ImPlotAxisFlags_NoDecorations);
                            ImPlot::SetupAxis(ImAxis_Y1, NULL, ImPlotAxisFlags_NoDecorations);

                            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0, displayBufSize);

                            ImPlot::SetupAxesLimits(0, displayBufSize, -1.5, 1.5, ImPlotCond_Once);

                            m_displayRingBufL.read(m_displayBuf, 0, displayBufSize);
                            ImPlot::PlotLine("##analyzer_plot_waveform_l", m_displayBuf, displayBufSize);

                            ImPlot::EndPlot();
                        }

                        if (!isMono) {
                            if (ImPlot::BeginPlot("Right##analyzer_plot_waveform_l", ImVec2(-1.0f, 0))) {

                                ImPlot::SetupAxis(ImAxis_X1, NULL, ImPlotAxisFlags_NoDecorations);
                                ImPlot::SetupAxis(ImAxis_Y1, NULL, ImPlotAxisFlags_NoDecorations);

                                ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0, displayBufSize);

                                ImPlot::SetupAxesLimits(0, displayBufSize, -1.5, 1.5, ImPlotCond_Once);

                                m_displayRingBufR.read(m_displayBuf, 0, displayBufSize);
                                ImPlot::PlotLine("##analyzer_plot_waveform_r", m_displayBuf, displayBufSize);

                                ImPlot::EndPlot();
                            }
                        }
                        ImPlot::EndSubplots();
                    }

                    ImGui::TableNextColumn();

                    static float ratiosSpec[] = { 1, 3 };
                    if (ImPlot::BeginSubplots("Spectrum##analyzer_spectrum_plots", 2, isMono ? 1 : 2, ImVec2(-1.0f, spaceAvail.y - ImGui::GetStyle().ItemSpacing.y),
                                              ImPlotSubplotFlags_ColMajor | ImPlotSubplotFlags_LinkAllX | ImPlotSubplotFlags_NoLegend, ratiosSpec)) {

                        if (m_fftDisplayBuf && m_processorL) {
                            if (ImPlot::BeginPlot("##analyzer_plot_fft_l", ImVec2(-1.0f, -1.0f))) {
                                m_processorL->readLatestFft(m_fftDisplayBuf, usefulBins);

                                ImPlot::SetupAxis(ImAxis_X1, "Hz", ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks);
                                ImPlot::SetupAxis(ImAxis_Y1, "dB");

                                ImPlot::SetupAxisScale(ImAxis_X1, displayMode == DisplayMode_log10 ? ImPlotScale_Log10 : ImPlotScale_Linear);

                                ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, binHz, sampleRate / 2.0);

                                ImPlot::SetupAxisLimits(ImAxis_X1, binHz, sampleRate / 2.0, ImPlotCond_Once);
                                ImPlot::SetupAxisLimits(ImAxis_Y1, -120.0, 0.0, ImPlotCond_Once);

                                if (dispModeChanged) {
                                    ImPlot::SetupAxisLimits(ImAxis_X1, binHz, sampleRate / 2.0, ImPlotCond_Always);
                                }

                                ImPlot::PlotLine("##analyzer_plot_fft_l", m_fftDisplayBuf + 1, usefulBins - 1, binHz);
                                ImPlot::PlotStems("##analyzer_plot_fft_l",
                                                  m_fftDisplayBuf + 1, usefulBins - 1, -120, binHz, 0, ImPlotStemsFlags_None);

                                ImPlot::EndPlot();
                            }
                        }

                        if (m_waterfallDisplayBuf && m_processorL) {
                            if (ImPlot::BeginPlot("##analyzer_plot_waterfall_l", ImVec2(-1.0f, -1.0f), ImPlotFlags_NoLegend)) {

                                m_waterfallRingBufL.read(m_waterfallDisplayBuf, 0, waterfallFreqBinCount * usefulBins);

                                ImPlot::SetupAxis(ImAxis_Y1, "", ImPlotAxisFlags_NoDecorations);
                                ImPlot::SetupAxis(ImAxis_X1, "Hz", ImPlotAxisFlags_Foreground);

                                ImPlot::SetupAxisScale(ImAxis_X1, displayMode == DisplayMode_log10 ? ImPlotScale_Log10 : ImPlotScale_Linear);

                                ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, binHz, sampleRate / 2.0);
                                ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, -1, 0);

                                ImPlot::SetupAxisLimits(ImAxis_X1, binHz, sampleRate / 2.0, ImPlotCond_Once);
                                ImPlot::SetupAxisLimits(ImAxis_Y1, -1, 0, ImPlotCond_Once);

                                if (dispModeChanged) {
                                    ImPlot::SetupAxisLimits(ImAxis_X1, binHz, sampleRate / 2.0, ImPlotCond_Always);
                                }

                                ImPlot::PushColormap(ImPlotColormap_Viridis);

                                ImPlot::PlotHeatmap("##analyzer_plot_waterfall_l",
                                                    m_waterfallDisplayBuf,
                                                    waterfallFreqBinCount,
                                                    usefulBins,
                                                    -100,
                                                    -10,
                                                    nullptr,
                                                    ImPlotPoint(binHz, 0),
                                                    ImPlotPoint(sampleRate / 2.0, -1),
                                                    ImPlotHeatmapFlags_None);

                                ImPlot::PopColormap();

                                ImPlot::EndPlot();
                            }
                        }

                        if (!isMono) {
                            if (m_fftDisplayBuf && m_processorR) {
                                if (ImPlot::BeginPlot("##analyzer_plot_fft_r", ImVec2(-1.0f, -1.0f))) {
                                    m_processorR->readLatestFft(m_fftDisplayBuf, usefulBins);

                                    ImPlot::SetupAxis(ImAxis_X1, "Hz", ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks);
                                    ImPlot::SetupAxis(ImAxis_Y1, "dB");

                                    ImPlot::SetupAxisScale(ImAxis_X1, displayMode == DisplayMode_log10 ? ImPlotScale_Log10 : ImPlotScale_Linear);

                                    ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, binHz, sampleRate / 2.0);

                                    ImPlot::SetupAxisLimits(ImAxis_X1, binHz, sampleRate / 2.0, ImPlotCond_Once);
                                    ImPlot::SetupAxisLimits(ImAxis_Y1, -120.0, 0.0, ImPlotCond_Once);

                                    if (dispModeChanged) {
                                        ImPlot::SetupAxisLimits(ImAxis_X1, binHz, sampleRate / 2.0, ImPlotCond_Always);
                                    }

                                    ImPlot::PlotLine("##analyzer_plot_fft_r", m_fftDisplayBuf + 1, usefulBins - 1, binHz);
                                    ImPlot::PlotStems("##analyzer_plot_fft_r",
                                                      m_fftDisplayBuf + 1, usefulBins - 1, -120, binHz, 0, ImPlotStemsFlags_None);

                                    ImPlot::EndPlot();
                                }
                            }

                            if (m_waterfallDisplayBuf && m_processorR) {
                                if (ImPlot::BeginPlot("##analyzer_plot_waterfall_r", ImVec2(-1.0f, -1.0f), ImPlotFlags_NoLegend)) {

                                    m_waterfallRingBufR.read(m_waterfallDisplayBuf, 0, waterfallFreqBinCount * usefulBins);

                                    ImPlot::SetupAxis(ImAxis_Y1, "", ImPlotAxisFlags_NoDecorations);
                                    ImPlot::SetupAxis(ImAxis_X1, "Hz", ImPlotAxisFlags_Foreground);

                                    ImPlot::SetupAxisScale(ImAxis_X1, displayMode == DisplayMode_log10 ? ImPlotScale_Log10 : ImPlotScale_Linear);

                                    ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, binHz, sampleRate / 2.0);
                                    ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, -1, 0);

                                    ImPlot::SetupAxisLimits(ImAxis_X1, binHz, sampleRate / 2.0, ImPlotCond_Once);
                                    ImPlot::SetupAxisLimits(ImAxis_Y1, -1, 0, ImPlotCond_Once);

                                    if (dispModeChanged) {
                                        ImPlot::SetupAxisLimits(ImAxis_X1, binHz, sampleRate / 2.0, ImPlotCond_Always);
                                    }

                                    ImPlot::PushColormap(ImPlotColormap_Viridis);

                                    ImPlot::PlotHeatmap("##analyzer_plot_waterfall_r",
                                                        m_waterfallDisplayBuf,
                                                        waterfallFreqBinCount,
                                                        usefulBins,
                                                        -100,
                                                        -10,
                                                        nullptr,
                                                        ImPlotPoint(binHz, 0),
                                                        ImPlotPoint(sampleRate / 2.0, -1),
                                                        ImPlotHeatmapFlags_None);

                                    ImPlot::PopColormap();

                                    ImPlot::EndPlot();
                                }
                            }
                        }

                        ImPlot::EndSubplots();
                    }
                    ImGui::EndTable();
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

        size_t tempBufSize = std::min(_this->m_displayBufSize, static_cast<size_t>(count));

        if (_this->m_mono) {
            dsp::complex_t* out = _this->m_outStreamL.writeBuf;
            float* temp = static_cast<float*>(malloc(tempBufSize * sizeof(float)));

            for (int i = 0; i < count; i++) {
                float re = (data[i].l + data[i].r) / 2.0f;
                out[i] = dsp::complex_t{ re, 0.0f };
                if (i < tempBufSize) {
                    temp[i] = re;
                }
            }

            _this->m_displayRingBufL.push(temp, tempBufSize);

            free(temp);

            if (!_this->m_outStreamL.swap(count)) flog::error("audio_analyzer failed to write data to outStreamL");

            return;
        }
        else {
            dsp::complex_t* outL = _this->m_outStreamL.writeBuf;
            dsp::complex_t* outR = _this->m_outStreamR.writeBuf;
            float* tempL = static_cast<float*>(malloc(tempBufSize * sizeof(float)));
            float* tempR = static_cast<float*>(malloc(tempBufSize * sizeof(float)));

            for (int i = 0; i < count; i++) {
                outL[i] = dsp::complex_t{ data[i].l, 0.0f };
                outR[i] = dsp::complex_t{ data[i].r, 0.0f };
                if (i < tempBufSize) {
                    tempL[i] = data[i].l;
                    tempR[i] = data[i].r;
                }
            }

            _this->m_displayRingBufL.push(tempL, tempBufSize);
            _this->m_displayRingBufR.push(tempR, tempBufSize);

            free(tempL);
            free(tempR);

            if (!_this->m_outStreamL.swap(count)) flog::error("audio_analyzer failed to write data to outStreamL");
            if (!_this->m_outStreamR.swap(count)) flog::error("audio_analyzer failed to write data to outStreamL");

            return;
        }

        flog::error("audio_analyzer failed; processors are nullptr");
    }

    void Analyzer::fftHandlerL(float* data, int count, void* ctx) {
        ZoneScoped;
        Analyzer* _this = static_cast<Analyzer*>(ctx);
        _this->m_waterfallRingBufL.push(data, count);
    }

    void Analyzer::fftHandlerR(float* data, int count, void* ctx) {
        ZoneScoped;
        Analyzer* _this = static_cast<Analyzer*>(ctx);
        _this->m_waterfallRingBufR.push(data, count);
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