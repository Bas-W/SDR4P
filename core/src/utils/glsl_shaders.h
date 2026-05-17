# pragma once
#include "opengl_include_code.h"

namespace glsl_shaders {
    char* get_shader_content(const char* fileName);
    void compile_shader(GLuint* shaderId, GLenum shaderType, const char* shaderFilePath);
    GLuint link_shader(GLuint vertexShaderID, GLuint fragmentShaderID);
};
