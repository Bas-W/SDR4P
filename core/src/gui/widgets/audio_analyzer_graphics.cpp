#include "audio_analyzer_graphics.h"
#include "Tracy.hpp"

namespace audio_analyzer_gfx {

    GLuint initTexture2D() {
        GLuint texture = 0;

        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        return texture;
    }

    void setTexture2DParams(const GLuint texture, const uint width, const uint height) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA8,
            width,
            height,
            0,
            GL_RGB,
            GL_UNSIGNED_BYTE,
            nullptr
        );
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    /** Renders audio waveform
     *
     * Renders an audio waveform based on the provided sample data, using a shader.
     *
     * @param data Pointer to the sample data
     * @param size Size of sample data buffer (# of samples)
     * @param params Parameters for shader handling
     * @param shaderInp Input (settings) to pass to the shader
     */
    void drawWaveForm(const float* data, const size_t size, const ComputeShaderParams& params, const WaveformShaderInput& shaderInp) {
        ZoneScoped;

        if (!data || !size || !params.buffer || !params.texture || !params.shader) {
            return;
        }

        glUniform1i(glGetUniformLocation(params.shader, "sampleCount"), static_cast<GLint>(size));
        glUniform1f(glGetUniformLocation(params.shader, "minVal"), shaderInp.minVal);
        glUniform1f(glGetUniformLocation(params.shader, "maxVal"), shaderInp.maxVal);
        glUniform1f(glGetUniformLocation(params.shader, "overlapRatio"), shaderInp.overlap);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, params.buffer);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, size * sizeof(float), data);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, params.buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        glBindTexture(GL_TEXTURE_2D, params.texture);
        glBindImageTexture(
            1,
            params.texture,
            0,
            GL_FALSE,
            0,
            GL_WRITE_ONLY,
            GL_RGBA8
        );

        int groupsX = (float)params.textureSize_x / (float)params.workGroupSize_x + 0.5f;
        int groupsY = (float)params.textureSize_y / (float)params.workGroupSize_y + 0.5f;
        int groupsZ = 1;

        glUseProgram(params.shader);
        glDispatchCompute(groupsX, groupsY, groupsZ);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }
}