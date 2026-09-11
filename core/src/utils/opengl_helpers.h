#pragma once
#include <cstdio>
#include <glad/glad.h>
#include "flog.h"

namespace opengl_helpers {
    inline GLuint loadShader(const char* path, uint16_t type) {
        flog::info("Loading shader file: \"{}\"", path);
        std::FILE* file = fopen(path, "r");
        if (!file) {
            flog::error("Failed to load shader file");
            return 0;
        }

        std::fseek(file, 0, SEEK_END);
        size_t size = std::ftell(file);
        std::rewind(file);

        if (!size > 0) {
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
        shader_id = glCreateShader(type);
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
}
