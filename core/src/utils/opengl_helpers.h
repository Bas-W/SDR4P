#pragma once
#include "utils/opengl_include_code.h"
#include <cstdio>
#include "flog.h"

namespace opengl_helpers {
    inline GLuint loadComputeShader(const char* path) {
        flog::info("Loading shader file: \"{}\"", path);
        std::FILE* file = fopen(path, "r");
        if (!file) {
            flog::error("Failed to load shader file");
            return 0;
        }

        std::fseek(file, 0, SEEK_END);
        size_t size = std::ftell(file);
        std::rewind(file);

        if (size <= 0) {
            flog::error("Shader file contents invalid");
            return 0;
        }

        char data[size];
        size_t read = std::fread(data, 1, size, file);
        std::fclose(file);

        if (read != size) {
            flog::error("Failed to read complete file");
            return 0;
        }

        GLuint shader_id = 0;
        shader_id = glCreateShader(GL_COMPUTE_SHADER);
        const GLchar* source = data;
        GLint sourceSize = static_cast<GLint>(size);
        glShaderSource(shader_id, 1, &source, &sourceSize);
        glCompileShader(shader_id);

        GLint rslt = GL_FALSE;
        glGetShaderiv(shader_id, GL_COMPILE_STATUS, &rslt);

        if (rslt != GL_TRUE) {
            GLint logLength = 0;
            glGetShaderiv(shader_id, GL_INFO_LOG_LENGTH, &logLength);

            char log[logLength + 1];
            glGetShaderInfoLog(shader_id, logLength, nullptr, (GLchar*)log);

            flog::error("Failed to compile shader \"{}\":\n{}", path, log);

            glDeleteShader(shader_id);
            return 0;
        }

        return shader_id;
    }

    inline GLuint linkComputeShader(GLuint shader) {
        if (!shader) {
            return 0;
        }
        GLuint program = glCreateProgram();
        glAttachShader(program, shader);
        glLinkProgram(program);
        return program;
    }

    class computeShader {
    public:
        GLuint m_shaderProgram;

        computeShader() {
            m_shaderProgram = 0;
        }

        ~computeShader() {
            unload();
        }

        bool load(const char* path) {
            if (m_shaderProgram) {
                unload();
            }
            GLuint shader = loadComputeShader(path);
            if (shader) {
                GLuint program = linkComputeShader(shader);
                glDeleteShader(shader);
                if (!program) {
                    return false;
                }
                m_shaderProgram = program;
                return true;
            }
            return false;
        }
        void unload() const {
            if (m_shaderProgram) {
                glDeleteProgram(m_shaderProgram);
            }
        }
    };
}
