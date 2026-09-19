#pragma once

#if defined(_WIN32)
#include <windows.h>
#include "glad/glad.h"
#elif defined(__APPLE__)
#include <OpenGL/gl.h>
#elif defined(__ANDROID__)
#include "glad/glad.h"
#include <EGL/egl.h>
#else
#include "glad/glad.h"
#include <GL/glext.h>
#endif
