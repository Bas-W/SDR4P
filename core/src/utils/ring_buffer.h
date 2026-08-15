#pragma once
#include "Tracy.hpp"
#include "flog.h"
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>

// Should be converted to template later

namespace rbuf {
    template <typename T>
    class SharedRingBuffer {
    public:
        ~SharedRingBuffer() {
            freeBuf();
        }

        /// Initialize ring buffer
        ///
        /// Param size: amount of values to keep (not bytes)
        void init(uint32_t size) {
            ZoneScoped;
            std::lock_guard<std::mutex> lck(m_mutex);

            if (m_buf) m_buf.reset();
            m_writeIdx = 0;
            m_size = size;
            m_buf = std::shared_ptr<T>(static_cast<T*>(std::malloc(size * sizeof(T))), free);
        }

        void freeBuf() {
            ZoneScoped;
            std::lock_guard<std::mutex> lck(m_mutex);
            m_buf.reset();
            m_size = 0;
        }

        /// Push data to ring buffer
        void push(T* data, uint32_t count) {
            ZoneScoped;
            std::lock_guard<std::mutex> lck(m_mutex);

            if (m_size < 1) return;

            if (m_writeIdx + count <= m_size) {
                std::memcpy(m_buf.get() + m_writeIdx, data, count * sizeof(T));
                m_writeIdx += count;
            }
            else {
                m_hasWrapped = true;
                uint32_t remaining = count;
                while (remaining > 0) {
                    uint32_t nToAdd = std::min(remaining, m_size - m_writeIdx);
                    std::memcpy(m_buf.get() + m_writeIdx, data + count - remaining, nToAdd * sizeof(T));
                    m_writeIdx = (m_writeIdx + nToAdd) % m_size;
                    remaining -= nToAdd;
                }
            }
        }

        /// Read from ringbuffer to regular buffer;
        void read(T* dest, uint32_t offset, uint32_t count) {
            ZoneScoped;
            std::lock_guard<std::mutex> lck(m_mutex);

            if (m_size < 1) return;

            uint32_t idxToRead = (m_hasWrapped ? m_writeIdx : 0) + offset % m_size;
            if (idxToRead + count <= m_size) {
                std::memcpy(dest, m_buf.get() + idxToRead, count * sizeof(T));
            }
            else {
                uint32_t remaining = count;
                while (remaining > 0) {
                    uint32_t nToRead = std::min(remaining, m_size - idxToRead);
                    std::memcpy(dest + count - remaining, m_buf.get() + idxToRead, nToRead * sizeof(T));
                    remaining -= nToRead;
                    idxToRead = 0;
                }
            }
        }

        uint32_t getSize() {
            ZoneScoped;
            std::lock_guard<std::mutex> lck(m_mutex);
            return m_size;
        }

    private:
        std::mutex m_mutex;
        std::shared_ptr<T> m_buf;
        uint32_t m_writeIdx = 0;
        uint32_t m_size = 0;
        bool m_hasWrapped = false;
    };
}