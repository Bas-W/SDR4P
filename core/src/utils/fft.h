#pragma once
#include <complex>
#include <cstdint>

namespace fft {
    void hammingWindow(const float* in, float* out, uint32_t n);
    void realToComplex(const float* in, std::complex<float>* out, uint32_t n);
    void bitReverse(std::complex<float>* data, uint32_t n);
    bool calcFft(const float* in, std::complex<float>* out, uint32_t n);
}
