#include "audio_analyzer.h"

#include <cmath>
#include <cstdlib>
#include <utility>
#include "Tracy.hpp"
#include "imgui.h"
#include "implot.h"
#include "signal_path/signal_path.h"
#include "utils/flog.h"

namespace audio_analyzer {
    void Processor::stereoHandler(dsp::stereo_t* data, int count, void* ctx) {
        Processor* _this = static_cast<Processor*>(ctx);
        if (_this->m_audioRingBufL.getSize() > 0 && _this->m_audioRingBufR.getSize() > 0) {
            _this->m_audioRingBufL.push(&data->l, count);
            _this->m_audioRingBufR.push(&data->r, count);
            _this->m_newSamples += count;
            _this->m_signalProcessFft.notify_one();
        }
    }

    void Processor::fftHandler() {
        while (m_fftHandlerShouldRun.load()) {
            std::unique_lock<std::mutex> lock(m_fftHandlerMutex);
            m_signalProcessFft.wait_for(lock, std::chrono::milliseconds(10), [this] {return m_newSamples.load() > m_fftSampleCount;});
            processFft();
        }
    }

    bool Processor::processFft() {
        if (m_newSamples < m_fftSampleCount) return false;

        while (m_newSamples >= m_fftSampleCount) {
            m_audioRingBufL.read(m_fftInBuf, m_audioRingBufL.getSize() - m_newSamples, m_fftSampleCount);
            fft::calcFft(m_fftInBuf, m_fftOutBufComplex, m_fftSampleCount);
            m_fftRingBufL.push(m_fftOutBufComplex, m_fftSampleCount / 2);

            m_audioRingBufR.read(m_fftInBuf, m_audioRingBufR.getSize() - m_newSamples, m_fftSampleCount);
            fft::calcFft(m_fftInBuf, m_fftOutBufComplex, m_fftSampleCount);
            m_fftRingBufR.push(m_fftOutBufComplex, m_fftSampleCount / 2);

            m_newSamples -= m_fftSampleCount;

            flog::debug("fft calculated");
        }

        return true;
    }

    void Processor::fftHandlerStart() {
        if (m_fftHandlerShouldRun.load() && m_fftHandlerThread.joinable()) return;
        m_fftHandlerShouldRun = true;
        m_fftHandlerThread = std::thread([this] {this->fftHandler();});
    }

    void Processor::fftHandlerStop() {
        m_fftHandlerShouldRun = false;
        if (m_fftHandlerThread.joinable()) m_fftHandlerThread.join();
    }

    Processor::Processor() {
        m_fftHandlerShouldRun = false;

        m_newSamples = 0;
        m_fftFreqBinSize = fftFreqBinSize_default;
        m_fftSampleCount = fftSampleCount_default;
        m_stereoSink.init(m_audioStream, stereoHandler, this);

        m_fftInBuf = static_cast<float*>(malloc(m_fftSampleCount * sizeof(float)));
        m_fftOutBufComplex = static_cast<std::complex<float>*>(malloc(m_fftSampleCount / 2 * sizeof(std::complex<float>)));
    }

    Processor::~Processor() {
        m_stereoSink.stop();
        free(m_fftInBuf);
    }

    void Processor::resizeBuffers(uint32_t size) {
        std::lock_guard<std::mutex> lck(m_mutex);

        m_audioRingBufL.freeBuf();
        m_audioRingBufR.freeBuf();
        m_fftRingBufL.freeBuf();
        m_fftRingBufR.freeBuf();

        m_audioRingBufL.init(size);
        m_audioRingBufR.init(size);
        m_fftRingBufL.init(size / 2.0f);
        m_fftRingBufR.init(size / 2.0f);
    }

    bool Processor::setAudioStream(dsp::stream<dsp::stereo_t>* stream, uint32_t streamRate) {
        if (!stream || streamRate == 0) return false;

        deselectStream();

        std::lock_guard<std::mutex> lck(m_mutex);

        m_audioStream = stream;
        m_streamRate = streamRate;

        m_fftInBufSize = m_streamRate / m_fftSampleRate;

        if (m_fftInBuf) free(m_fftInBuf);
        m_fftInBuf = static_cast<float*>(malloc(m_fftInBufSize * sizeof(float)));

        m_stereoSink.setInput(m_audioStream);
        m_stereoSink.start();

        fftHandlerStart();

        return true;
    }

    void Processor::deselectStream() {
        std::lock_guard<std::mutex> lck(m_mutex);
        m_stereoSink.stop();
        fftHandlerStop();
        m_audioStream = NULL;
    }

    ProcessorDisplay::ProcessorDisplay(std::shared_ptr<Processor> processor, uint32_t size) {
        m_processor = std::move(processor);
        resizeBuffers(size);
    }

    void ProcessorDisplay::setAudioStreams(std::shared_ptr<OptionList<std::string, std::string>> audioStreams) {
        m_audioStreams = audioStreams;
    }

    void ProcessorDisplay::draw() {
        if (ImGui::Combo("##_recorder_stream", &m_audioStreamId, m_audioStreams->txt)) {
            m_processor->setAudioStream(
                sigpath::sinkManager.bindStream(m_audioStreams->value(m_audioStreamId)),
                sigpath::sinkManager.getStreamSampleRate(m_audioStreams->name(m_audioStreamId))
                );
        }

        if (ImPlot::BeginAlignedPlots("Signal")) {
            static ImPlotRange xRange{0.0, static_cast<double>(bufferSize())};
            static ImPlotRange yRange{-1.0, 1.0};

            updateBuffers();

            float ySize = ImGui::GetContentRegionAvail().y;

            if (ImPlot::BeginPlot("Left", ImVec2(-1, ySize / 2.0f))) {
                ImPlot::SetupAxisLinks(ImAxis_X1, &xRange.Min, &xRange.Max);
                ImPlot::SetupAxisLinks(ImAxis_Y1, &yRange.Min, &yRange.Max);
                ImPlot::PlotLine("Left", getBufferLeft().get(), bufferSize());
                ImPlot::EndPlot();
            }

            /*
            if (ImPlot::BeginPlot("Right", ImVec2(-1, ySize / 2.0f))) {
                ImPlot::SetupAxisLinks(ImAxis_X1, &xRange.Min, &xRange.Max);
                ImPlot::SetupAxisLinks(ImAxis_Y1, &yRange.Min, &yRange.Max);
                ImPlot::PlotLine("Right", getBufferRight().get(), bufferSize());
                ImPlot::EndPlot();
            }
            */

            std::vector<float> fftL(m_bufferSize / 2 / 100);

            for (uint32_t i = 0; i < fftL.size(); i++) {
                fftL[i] = std::abs(m_fftBufferL.get()[i].real());
            }

            if (ImPlot::BeginPlot("FFT-L", ImVec2(-1, 300))) {
                ImPlot::SetupAxes(
                    "Frequency Bin",
                    "FFT Frame"
                );

                ImPlot::PlotHeatmap(
                    "FFT",
                    fftL.data(),
                    fftSampleCount_default / 2,
                    fftL.size() / ( fftSampleCount_default / 2),
                    0,
                    1,
                    "%.1f",
                    ImPlotPoint(0, 0),
                    ImPlotPoint(1, 1),
                    ImPlotHeatmapFlags_ColMajor
                );

                ImPlot::EndPlot();
            }

            ImPlot::EndAlignedPlots();
        }
    }

    void ProcessorDisplay::resizeBuffers(uint32_t size) {
        if (m_bufferL != nullptr) m_bufferL.reset();
        if (m_bufferR != nullptr) m_bufferR.reset();
        if (m_fftBufferL != nullptr) m_fftBufferL.reset();
        if (m_fftBufferR != nullptr) m_fftBufferR.reset();
        m_bufferL = std::shared_ptr<float>(static_cast<float*>(malloc(size * sizeof(float))), free);
        m_bufferR = std::shared_ptr<float>(static_cast<float*>(malloc(size * sizeof(float))), free);
        m_fftBufferL = std::shared_ptr<std::complex<float>>(static_cast<std::complex<float>*>(malloc(size / 2 * sizeof(std::complex<float>))), free);
        m_fftBufferR = std::shared_ptr<std::complex<float>>(static_cast<std::complex<float>*>(malloc(size / 2 * sizeof(std::complex<float>))), free);
        m_bufferSize = size;
    }

    void ProcessorDisplay::updateBuffers() {
        m_processor->m_audioRingBufL.read(m_bufferL.get(), 0, m_bufferSize);
        m_processor->m_audioRingBufR.read(m_bufferR.get(), 0, m_bufferSize);
        m_processor->m_fftRingBufL.read(m_fftBufferL.get(), 0, m_bufferSize / 2);
        m_processor->m_fftRingBufR.read(m_fftBufferR.get(), 0, m_bufferSize / 2);
    }

    std::shared_ptr<const float> ProcessorDisplay::getBufferLeft() {
        return m_bufferL;
    }
    std::shared_ptr<const float> ProcessorDisplay::getBufferRight() {
        return m_bufferR;
    }

    const uint32_t ProcessorDisplay::bufferSize() {
        return m_bufferSize;
    }

    AudioAnalyzer::AudioAnalyzer() {
        m_audioStreams = std::make_shared<OptionList<std::string, std::string>>();
    }

    void AudioAnalyzer::doPostInit() {
        m_audioStreams->clear();
        auto names = sigpath::sinkManager.getStreamNames();
        for (const auto& name : names) {
            m_audioStreams->define(name, name, name);
        }
    }

    void AudioAnalyzer::addProcessorDisplay() {
        std::shared_ptr<audio_analyzer::Processor> processor = std::make_shared<audio_analyzer::Processor>();
        processor->resizeBuffers(480000);

        std::shared_ptr<audio_analyzer::ProcessorDisplay> processorDisplay = std::make_shared<audio_analyzer::ProcessorDisplay>(processor, 480000);

        processorDisplay->setAudioStreams(m_audioStreams);
        m_processorDisplays.push_back(processorDisplay);
    }

    void AudioAnalyzer::draw() {
        ZoneScoped;
        if (ImGui::BeginChild("##srdpp_audioAnalyzer")) {
            if (ImGui::Button("Add")) {
                addProcessorDisplay();
            }
            for (int i = 0; i < m_processorDisplays.size(); i++) {
                ImGui::BeginChild(("##audioAnalyzer_processor_" + std::to_string(i)).c_str());
                m_processorDisplays[i]->draw();
                ImGui::EndChild();
            }
        }
        ImGui::EndChild();
    }
}