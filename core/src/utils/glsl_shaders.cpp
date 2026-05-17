#include "glsl_shaders.h"
#include <stdio.h>
#include <string.h>
#include "flog.h"

namespace glsl_shaders {
    char* get_shader_content(const char* fileName)
    {
        FILE *fp;
        long size = 0;
        char* shaderContent;

        fp = fopen(fileName, "rb");
        if(fp == NULL) {
            return "";
        }
        fseek(fp, 0L, SEEK_END);
        size = ftell(fp)+1;
        fclose(fp);

        /* Read File for Content */
        fp = fopen(fileName, "r");
        shaderContent = static_cast<char*>(memset(malloc(size), '\0', size));
        fread(shaderContent, 1, size-1, fp);
        fclose(fp);

        return shaderContent;
    }

    void compile_shader(GLuint* shaderId, GLenum shaderType, const char* shaderFilePath)
    {
        GLint isCompiled = 0;
        const char* shaderSource = get_shader_content(shaderFilePath);

        *shaderId = glCreateShader(shaderType);
        if(*shaderId == 0) {
            flog::error("COULD NOT LOAD SHADER: {}!", shaderFilePath);
        }

        glShaderSource(*shaderId, 1, (const char**)&shaderSource, NULL);
        glCompileShader(*shaderId);
        glGetShaderiv(*shaderId, GL_COMPILE_STATUS, &isCompiled);

        if(isCompiled == GL_FALSE) {
            flog::error("Shader Compiler Error: {}", shaderFilePath);
            glDeleteShader(*shaderId);
            return;
        }
    }

    GLuint link_shader(GLuint vertexShaderID, GLuint fragmentShaderID)
    {
        GLuint programID = 0;
        GLint isLinked = 0;
        GLint maxLength = 0;
        char* infoLog = static_cast<char*>(malloc(1024));

        programID = glCreateProgram();

        glAttachShader(programID, vertexShaderID);
        glAttachShader(programID, fragmentShaderID);

        glLinkProgram(programID);

        glGetProgramiv(programID, GL_LINK_STATUS, &isLinked);
        if(isLinked == GL_FALSE) {
            flog::error("Shader Program Linker Error");

            glGetProgramiv(programID, GL_INFO_LOG_LENGTH, &maxLength);
            glGetProgramInfoLog(programID, maxLength, &maxLength, &infoLog[0]);

            flog::info("{}", infoLog);

            glDeleteProgram(programID);

            glDeleteShader(vertexShaderID);
            glDeleteShader(fragmentShaderID);
            free(infoLog);

            return 0;
        }

        glDetachShader(programID, vertexShaderID);
        glDetachShader(programID, fragmentShaderID);

        glDeleteShader(vertexShaderID);
        glDeleteShader(fragmentShaderID);
        free(infoLog);

        return programID;
    }
}
