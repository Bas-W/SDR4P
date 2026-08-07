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
    static void stereoHandler(dsp::stereo_t* data, int count, void* ctx) {
        Processor* _this = static_cast<Processor*>(ctx);
        if (_this->m_ringBufL.getSize() > 0) {
            _this->m_ringBufL.push(&data->l, count);
        }
        if (_this->m_ringBufR.getSize() > 0) {
            _this->m_ringBufR.push(&data->r, count);
        }
    }
    Processor::Processor() {
        m_stereoSink.init(m_audioStream, stereoHandler, this);
    }
    Processor::~Processor() {
        m_stereoSink.stop();
    }

    void Processor::resizeBuffers(uint32_t size) {
        std::lock_guard<std::recursive_mutex> lck(m_recMtx);

        m_ringBufL.freeBuf();
        m_ringBufR.freeBuf();

        m_ringBufL.init(size);
        m_ringBufR.init(size);
    }

    bool Processor::setAudioStream(dsp::stream<dsp::stereo_t>* stream) {
        if (!stream) return false;

        std::lock_guard<std::recursive_mutex> lck(m_recMtx);
        deselectStream();

        m_audioStream = stream;

        m_stereoSink.setInput(m_audioStream);
        m_stereoSink.start();

        return true;
    }

    void Processor::deselectStream() {
        std::lock_guard<std::recursive_mutex> lck(m_recMtx);
        m_stereoSink.stop();
        m_audioStream = NULL;
    }

    ProcessorWorker::~ProcessorWorker() {
        stop(true);
    }

    void ProcessorWorker::init(std::shared_ptr<Processor> processor) {
        m_processor = std::move(processor);
    }

    void ProcessorWorker::start() {
        m_shouldRun.store(true);
        m_thread = std::thread(&ProcessorWorker::process, this);
    }

    void ProcessorWorker::stop(bool joinThread) {
        m_shouldRun.store(false);
        if (joinThread) m_thread.join();
    }

    /// Process the buffer
    void ProcessorWorker::process() {

        // Nasty test code, temporary

        float iterator = 1.0f;
        static float* buf = static_cast<float*>(malloc(4800 * sizeof(float)));
        while (m_shouldRun.load()) {
            for (int i = 0; i < 4800; i++) {
                buf[i] = std::sin(iterator / 100);
                iterator = iterator * 1.5;
                if (iterator > 100000) iterator = 1.0f;
            }

            m_processor->m_ringBufL.push(buf, 4800);

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        free(buf);
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
            m_processor->setAudioStream(sigpath::sinkManager.bindStream(m_audioStreams->value(m_audioStreamId)));
        }

        if (ImPlot::BeginAlignedPlots("Signal")) {
            static ImPlotRange xRange, yRange;

            updateBuffers();

            float ySize = ImGui::GetContentRegionAvail().y;

            if (ImPlot::BeginPlot("Left", ImVec2(-1, ySize / 2.0f))) {
                ImPlot::SetupAxisLinks(ImAxis_X1, &xRange.Min, &xRange.Max);
                ImPlot::SetupAxisLinks(ImAxis_Y1, &yRange.Min, &yRange.Max);
                ImPlot::PlotLine("Left", getBufferLeft().get(), bufferSize());
                ImPlot::EndPlot();
            }

            if (ImPlot::BeginPlot("Right", ImVec2(-1, ySize / 2.0f))) {
                ImPlot::SetupAxisLinks(ImAxis_X1, &xRange.Min, &xRange.Max);
                ImPlot::SetupAxisLinks(ImAxis_Y1, &yRange.Min, &yRange.Max);
                ImPlot::PlotLine("Right", getBufferRight().get(), bufferSize());
                ImPlot::EndPlot();
            }

            ImPlot::EndAlignedPlots();
        }
    }

    void ProcessorDisplay::resizeBuffers(uint32_t size) {
        if (m_bufferL != nullptr) m_bufferL.reset();
        if (m_bufferR != nullptr) m_bufferR.reset();
        m_bufferL = std::shared_ptr<float>(static_cast<float*>(malloc(size * sizeof(float))), free);
        m_bufferR = std::shared_ptr<float>(static_cast<float*>(malloc(size * sizeof(float))), free);
        m_bufferSize = size;
    }

    void ProcessorDisplay::updateBuffers() {
        m_processor->m_ringBufL.read(m_bufferL.get(), 0, m_bufferSize);
        m_processor->m_ringBufR.read(m_bufferR.get(), 0, m_bufferSize);
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
    void AudioAnalyzer::addProcessorDisplay(std::shared_ptr<ProcessorDisplay> processorDisplay) {
        processorDisplay->setAudioStreams(m_audioStreams);
        m_processorDisplays.push_back(processorDisplay);
    }
    void AudioAnalyzer::draw() {
        ZoneScoped;
        if (ImGui::BeginChild("##srdpp_audioAnalyzer")) {
            for (int i = 0; i < m_processorDisplays.size(); i++) {
                ImGui::BeginChild(("##audioAnalyzer_processor_" + std::to_string(i)).c_str());
                m_processorDisplays[i]->draw();
                ImGui::EndChild();
            }
        }
        ImGui::EndChild();
    }
}