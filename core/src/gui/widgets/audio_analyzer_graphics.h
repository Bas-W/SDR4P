#pragma once
#include "utils/opengl_helpers.h"

namespace audio_analyzer_gfx {
    struct Shader {
        std::string name;
        opengl_helpers::computeShader shader;
    };

    struct ComputeShaderParams {
        GLuint buffer;
        GLuint texture;
        GLuint shader;
        GLuint workGroupSize_x;
        GLuint workGroupSize_y;
        GLuint textureSize_x;
        GLuint textureSize_y;
    };

    struct WaveformShaderInput {
        float minVal;
        float maxVal;
        float overlap;
    };

    GLuint initTexture2D();
    void setTexture2DParams(GLuint texture, uint width, uint height);

    void drawWaveForm(const float* data, size_t size, const ComputeShaderParams& params, const WaveformShaderInput& shaderInp);
}