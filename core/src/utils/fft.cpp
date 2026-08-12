#define _USE_MATH_DEFINES

#include "fft.h"
#include "math.h"

#include <cstring>

//@todo: ability to generate factors once and use matrix operations
void fft::hammingWindow(const float* in, float* out, uint32_t n) {
    const float a0 = 0.53836f;
    const float a1 = 0.46164f;

    for (int i = 0; i < n; i++) {
        out[i] = (a0 - a1 * std::cos(2 * M_PI * i / n)) * in[i];
    }
}

void fft::realToComplex(const float* in, std::complex<float>* out, const uint32_t n) {
    if (in == nullptr || out == nullptr) return;

    for (int i = 0; i < n; i++) {
        out[i] = std::complex<float>{in[i], 0.0f};
    }
}

void fft::bitReverse(std::complex<float>* data, uint32_t n)
{
    uint32_t j = 0;

    for (uint32_t i = 1; i < n; ++i) {
        uint32_t bit = n >> 1;

        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }

        j ^= bit;

        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }
}

/// Will write n/2 bytes to out buffer
bool fft::calcFft(const float* in, std::complex<float>* out, uint32_t n) {
    if (in == nullptr || out == nullptr) return false;
    if (n == 0 || (n & (n - 1)) != 0) return false;

    float* buf = static_cast<float*>(malloc(n * sizeof(float)));
    std::complex<float>* bufComp = static_cast<std::complex<float>*>(malloc(n * sizeof(std::complex<float>)));

    hammingWindow(in, buf, n);
    realToComplex(buf, bufComp, n);
    bitReverse(bufComp, n);

    for (uint32_t k = 2; k <= n; k <<= 1) {
        const float angle = -2.0f * M_PI / static_cast<float>(k);
        const std::complex<float> wK(std::cos(angle), std::sin(angle));
        for (uint32_t i = 0; i < n; i += k) {
            std::complex<float> w(1.0f, 0.0f);
            for (uint32_t j = 0; j < k / 2; j++) {
                uint32_t evenIdx = i + j;
                uint32_t oddIdx = i + j + k / 2;

                std::complex<float> u = bufComp[evenIdx];
                std::complex<float> t = w * bufComp[oddIdx];

                bufComp[evenIdx] = u + t;
                bufComp[oddIdx]  = u - t;

                w *= wK;
            }
        }
    }

    std::memcpy(out, bufComp, n * sizeof(std::complex<float>) / 2);

    free(buf);
    free(bufComp);

    return true;
}