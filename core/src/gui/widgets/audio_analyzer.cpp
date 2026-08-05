//
// Created by Bas on 05/08/2026.
//

#include "audio_analyzer.h"

#include "Tracy.hpp"
#include "utils/flog.h"

#include <cstdlib>
#include <cstring>

namespace audio_analyzer {
    AudioAnalyzer::~AudioAnalyzer() {
        audioRingBufFree();
    }

    /// Initialize ring buffer
    ///
    /// Param size: amount of values to keep (not bytes)
    void AudioAnalyzer::audioRingBufInit(uint32_t size) {
        ZoneScoped;
        std::lock_guard<std::mutex> lck(m_audioRingBufMtx);

        m_audioRingBufIdx = 0;
        m_audioRingBufSize = size;
        m_audioRingBuf = static_cast<float*>(std::malloc(size * sizeof(float)));
    }

    void AudioAnalyzer::audioRingBufFree() {
        ZoneScoped;
        std::lock_guard<std::mutex> lck(m_audioRingBufMtx);
        std::free(m_audioRingBuf);
        m_audioRingBuf = nullptr;
        m_audioRingBufSize = 0;
    }

    /// Push data to ring buffer
    void AudioAnalyzer::audioRingBufPush(float* data, uint32_t count) {
        ZoneScoped;
        std::lock_guard<std::mutex> lck(m_audioRingBufMtx);

        if (m_audioRingBufIdx + count < m_audioRingBufSize) {
            std::memcpy(m_audioRingBuf + m_audioRingBufIdx, data, count * sizeof(float));
            m_audioRingBufIdx += count;
        }
        else {
            uint32_t remaining = count;
            while (remaining > 0) {
                uint32_t nToAdd = std::min(remaining, m_audioRingBufSize - m_audioRingBufIdx);
                std::memcpy(m_audioRingBuf + m_audioRingBufIdx, data + count - remaining, nToAdd * sizeof(float));
                m_audioRingBufIdx = (m_audioRingBufIdx + nToAdd) % m_audioRingBufSize;
                remaining -= nToAdd;
            }
        }
    }

    /// Read from ringbuffer to regular buffer;
    void AudioAnalyzer::audioRingBufRead(float* dest, uint32_t offset, uint32_t count) {
        ZoneScoped;
        std::lock_guard<std::mutex> lck(m_audioRingBufMtx);

        if (m_audioRingBufIdx + offset + count < m_audioRingBufSize) {
            std::memcpy(dest, m_audioRingBuf + m_audioRingBufIdx + offset, count * sizeof(float));
        }
        else {
            uint32_t remaining = count;
            uint32_t idxToRead = m_audioRingBufIdx + offset;
            while (remaining > 0) {
                uint32_t nToRead = std::min(remaining, m_audioRingBufSize - idxToRead);
                std::memcpy(dest + count - remaining, m_audioRingBuf + idxToRead, nToRead * sizeof(float));
                remaining -= nToRead;
                idxToRead = remaining % m_audioRingBufSize;
            }
        }
    }
}