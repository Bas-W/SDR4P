#include "ring_buffer.h"

#include "Tracy.hpp"
#include "flog.h"
#include <cstring>

namespace rbuf {
    SharedRingBuffer::~SharedRingBuffer() {
        freeBuf();
    }

    /// Initialize ring buffer
    ///
    /// Param size: amount of values to keep (not bytes)
    void SharedRingBuffer::init(uint32_t size) {
        ZoneScoped;
        std::lock_guard<std::mutex> lck(m_mutex);

        m_writeIdx = 0;
        m_size = size;
        m_buf = std::shared_ptr<float>(static_cast<float*>(std::malloc(size * sizeof(float))), free);
    }

    void SharedRingBuffer::freeBuf() {
        ZoneScoped;
        std::lock_guard<std::mutex> lck(m_mutex);
        m_buf.reset();
        m_size = 0;
    }

    /// Push data to ring buffer
    void SharedRingBuffer::push(float* data, uint32_t count) {
        ZoneScoped;
        std::lock_guard<std::mutex> lck(m_mutex);

        flog::debug("idx: {}", m_writeIdx);
        if (m_writeIdx + count <= m_size) {
            std::memcpy(m_buf.get() + m_writeIdx, data, count * sizeof(float));
            m_writeIdx += count;
        }
        else {
            m_hasWrapped = true;
            uint32_t remaining = count;
            while (remaining > 0) {
                uint32_t nToAdd = std::min(remaining, m_size - m_writeIdx);
                std::memcpy(m_buf.get() + m_writeIdx, data + count - remaining, nToAdd * sizeof(float));
                m_writeIdx = (m_writeIdx + nToAdd) % m_size;
                remaining -= nToAdd;
            }
        }
    }

    /// Read from ringbuffer to regular buffer;
    void SharedRingBuffer::read(float* dest, uint32_t offset, uint32_t count) {
        ZoneScoped;
        std::lock_guard<std::mutex> lck(m_mutex);

        uint32_t idxToRead = (m_hasWrapped ? m_writeIdx : 0) + offset;
        if (idxToRead + count <= m_size) {
            std::memcpy(dest, m_buf.get() + idxToRead, count * sizeof(float));
        }
        else {
            uint32_t remaining = count;
            while (remaining > 0) {
                uint32_t nToRead = std::min(remaining, m_size - idxToRead);
                std::memcpy(dest + count - remaining, m_buf.get() + idxToRead, nToRead * sizeof(float));
                remaining -= nToRead;
                idxToRead = remaining % m_size;
            }
        }
    }
    uint32_t SharedRingBuffer::getSize() {
        ZoneScoped;
        std::lock_guard<std::mutex> lck(m_mutex);
        return m_size;
    }
}