#pragma once
#include <cstdint>
#include <memory>
#include <mutex>

// Should be converted to template later

namespace rbuf {
    class SharedRingBuffer {
    public:
        ~SharedRingBuffer();

        void init(uint32_t size);
        void freeBuf();
        void push(float* data, uint32_t count);
        void read(float* dest, uint32_t offset, uint32_t count);

        uint32_t getSize();

    private:
        std::mutex m_mutex;
        std::shared_ptr<float> m_buf;
        uint32_t m_writeIdx = 0;
        uint32_t m_size = 0;
        bool m_hasWrapped = false;
    };
}