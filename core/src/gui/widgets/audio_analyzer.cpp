//
// Created by Bas on 05/08/2026.
//

#include "audio_analyzer.h"

#include "Tracy.hpp"
#include "imgui.h"
#include "implot.h"
#include "utils/flog.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace audio_analyzer {
    Processor::~Processor() {
        audioRingBufFree();
    }

    /// Initialize ring buffer
    ///
    /// Param size: amount of values to keep (not bytes)
    void Processor::audioRingBufInit(uint32_t size) {
        ZoneScoped;
        std::lock_guard<std::mutex> lck(m_audioRingBufMtx);

        m_audioRingBufIdx = 0;
        m_audioRingBufSize = size;
        m_audioRingBuf = std::shared_ptr<float>(static_cast<float*>(std::malloc(size * sizeof(float))), free);
    }

    void Processor::audioRingBufFree() {
        ZoneScoped;
        std::lock_guard<std::mutex> lck(m_audioRingBufMtx);
        m_audioRingBuf.reset();
        m_audioRingBufSize = 0;
    }

    /// Push data to ring buffer
    void Processor::audioRingBufPush(float* data, uint32_t count) {
        ZoneScoped;
        std::lock_guard<std::mutex> lck(m_audioRingBufMtx);

        flog::debug("idx: {}", m_audioRingBufIdx);
        if (m_audioRingBufIdx + count <= m_audioRingBufSize) {
            std::memcpy(m_audioRingBuf.get() + m_audioRingBufIdx, data, count * sizeof(float));
            m_audioRingBufIdx += count;
        }
        else {
            m_audioRingBufWrapped = true;
            uint32_t remaining = count;
            while (remaining > 0) {
                uint32_t nToAdd = std::min(remaining, m_audioRingBufSize - m_audioRingBufIdx);
                std::memcpy(m_audioRingBuf.get() + m_audioRingBufIdx, data + count - remaining, nToAdd * sizeof(float));
                m_audioRingBufIdx = (m_audioRingBufIdx + nToAdd) % m_audioRingBufSize;
                remaining -= nToAdd;
            }
        }
    }

    /// Read from ringbuffer to regular buffer;
    void Processor::audioRingBufRead(float* dest, uint32_t offset, uint32_t count) {
        ZoneScoped;
        std::lock_guard<std::mutex> lck(m_audioRingBufMtx);

        uint32_t idxToRead = (m_audioRingBufWrapped ? m_audioRingBufIdx : 0) + offset;
        if (idxToRead + count <= m_audioRingBufSize) {
            std::memcpy(dest, m_audioRingBuf.get() + idxToRead, count * sizeof(float));
        }
        else {
            uint32_t remaining = count;
            while (remaining > 0) {
                uint32_t nToRead = std::min(remaining, m_audioRingBufSize - idxToRead);
                std::memcpy(dest + count - remaining, m_audioRingBuf.get() + idxToRead, nToRead * sizeof(float));
                remaining -= nToRead;
                idxToRead = remaining % m_audioRingBufSize;
            }
        }
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

            m_processor->audioRingBufPush(buf, 4800);

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        free(buf);
    }

    ProcessorDisplay::ProcessorDisplay(std::shared_ptr<Processor> processor, uint32_t size) {
        m_processor = std::move(processor);
        resizeBuffer(size);
    }

    void ProcessorDisplay::resizeBuffer(uint32_t size) {
        if (m_buffer != nullptr) m_buffer.reset();
        m_buffer = std::shared_ptr<float>(static_cast<float*>(malloc(size * sizeof(float))), free);
        m_bufferSize = size;
    }

    void ProcessorDisplay::updateBuffer() {
        m_processor->audioRingBufRead(m_buffer.get(), 0, m_bufferSize);
    }

    std::shared_ptr<const float> ProcessorDisplay::getBuffer() {
        return m_buffer;
    }
    const uint32_t ProcessorDisplay::bufferSize() {
        return m_bufferSize;
    }

    void AudioAnalyzer::addProcessorDisplay(std::shared_ptr<ProcessorDisplay> processorDisplay) {
        m_processorDisplays.push_back(processorDisplay);
    }
    void AudioAnalyzer::draw() {
        ZoneScoped;
        if (ImGui::BeginChild("##srdpp_audioAnalyzer")) {
            for (int i = 0; i < m_processorDisplays.size(); i++) {
                if (ImPlot::BeginPlot(("##audioAnalyzer_plot_" + std::to_string(i)).c_str(), ImVec2(-1, 0), ImPlotFlags_None)) {
                    m_processorDisplays[i]->updateBuffer();

                    ImPlot::PlotLine("Signal", m_processorDisplays[i]->getBuffer().get(), m_processorDisplays[i]->bufferSize());

                    ImPlot::EndPlot();
                }
            }
        }
        ImGui::EndChild();
    }
}