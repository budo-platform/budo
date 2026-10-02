#ifndef BUDO_GL_DESKTOP_H
#define BUDO_GL_DESKTOP_H

#include <stdbool.h>

#if defined(_WIN32) && !defined(BUDO_GL_DESKTOP_LOADER)
#define BUDO_GL_DESKTOP_LOADER 1
#endif

#ifndef BUDO_GL_DESKTOP_LOADER
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_opengl_glext.h>

#ifdef __cplusplus
extern "C"
{
#endif

    bool budo_gl_desktop_load(void);

#ifdef BUDO_GL_DESKTOP_LOADER

#define BUDO_GL_DESKTOP_FUNCTIONS(X) \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture) \
    X(PFNGLATTACHSHADERPROC, glAttachShader) \
    X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation) \
    X(PFNGLBINDBUFFERPROC, glBindBuffer) \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer) \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
    X(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate) \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) \
    X(PFNGLBUFFERDATAPROC, glBufferData) \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
    X(PFNGLCOMPILESHADERPROC, glCompileShader) \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    X(PFNGLCREATESHADERPROC, glCreateShader) \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram) \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers) \
    X(PFNGLDELETESHADERPROC, glDeleteShader) \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays) \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray) \
    X(PFNGLDRAWARRAYSINSTANCEDPROC, glDrawArraysInstanced) \
    X(PFNGLDRAWELEMENTSINSTANCEDPROC, glDrawElementsInstanced) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) \
    X(PFNGLGENBUFFERSPROC, glGenBuffers) \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers) \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) \
    X(PFNGLGETATTRIBLOCATIONPROC, glGetAttribLocation) \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv) \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage) \
    X(PFNGLSHADERSOURCEPROC, glShaderSource) \
    X(PFNGLUNIFORM1FPROC, glUniform1f) \
    X(PFNGLUNIFORM1FVPROC, glUniform1fv) \
    X(PFNGLUNIFORM1IPROC, glUniform1i) \
    X(PFNGLUNIFORM1IVPROC, glUniform1iv) \
    X(PFNGLUNIFORM2FPROC, glUniform2f) \
    X(PFNGLUNIFORM2FVPROC, glUniform2fv) \
    X(PFNGLUNIFORM3FPROC, glUniform3f) \
    X(PFNGLUNIFORM3FVPROC, glUniform3fv) \
    X(PFNGLUNIFORM4FPROC, glUniform4f) \
    X(PFNGLUNIFORM4FVPROC, glUniform4fv) \
    X(PFNGLUNIFORMMATRIX3FVPROC, glUniformMatrix3fv) \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv) \
    X(PFNGLUSEPROGRAMPROC, glUseProgram) \
    X(PFNGLVERTEXATTRIBDIVISORPROC, glVertexAttribDivisor) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)

#define BUDO_GL_DESKTOP_DECLARE(type, name) extern type budo_##name;
    BUDO_GL_DESKTOP_FUNCTIONS(BUDO_GL_DESKTOP_DECLARE)
#undef BUDO_GL_DESKTOP_DECLARE

#define glActiveTexture budo_glActiveTexture
#define glAttachShader budo_glAttachShader
#define glBindAttribLocation budo_glBindAttribLocation
#define glBindBuffer budo_glBindBuffer
#define glBindFramebuffer budo_glBindFramebuffer
#define glBindRenderbuffer budo_glBindRenderbuffer
#define glBindVertexArray budo_glBindVertexArray
#define glBlendEquationSeparate budo_glBlendEquationSeparate
#define glBlendFuncSeparate budo_glBlendFuncSeparate
#define glBufferData budo_glBufferData
#define glBufferSubData budo_glBufferSubData
#define glCheckFramebufferStatus budo_glCheckFramebufferStatus
#define glCompileShader budo_glCompileShader
#define glCreateProgram budo_glCreateProgram
#define glCreateShader budo_glCreateShader
#define glDeleteBuffers budo_glDeleteBuffers
#define glDeleteFramebuffers budo_glDeleteFramebuffers
#define glDeleteProgram budo_glDeleteProgram
#define glDeleteRenderbuffers budo_glDeleteRenderbuffers
#define glDeleteShader budo_glDeleteShader
#define glDeleteVertexArrays budo_glDeleteVertexArrays
#define glDisableVertexAttribArray budo_glDisableVertexAttribArray
#define glDrawArraysInstanced budo_glDrawArraysInstanced
#define glDrawElementsInstanced budo_glDrawElementsInstanced
#define glEnableVertexAttribArray budo_glEnableVertexAttribArray
#define glFramebufferRenderbuffer budo_glFramebufferRenderbuffer
#define glFramebufferTexture2D budo_glFramebufferTexture2D
#define glGenBuffers budo_glGenBuffers
#define glGenFramebuffers budo_glGenFramebuffers
#define glGenRenderbuffers budo_glGenRenderbuffers
#define glGenVertexArrays budo_glGenVertexArrays
#define glGetAttribLocation budo_glGetAttribLocation
#define glGetProgramInfoLog budo_glGetProgramInfoLog
#define glGetProgramiv budo_glGetProgramiv
#define glGetShaderInfoLog budo_glGetShaderInfoLog
#define glGetShaderiv budo_glGetShaderiv
#define glGetUniformLocation budo_glGetUniformLocation
#define glLinkProgram budo_glLinkProgram
#define glRenderbufferStorage budo_glRenderbufferStorage
#define glShaderSource budo_glShaderSource
#define glUniform1f budo_glUniform1f
#define glUniform1fv budo_glUniform1fv
#define glUniform1i budo_glUniform1i
#define glUniform1iv budo_glUniform1iv
#define glUniform2f budo_glUniform2f
#define glUniform2fv budo_glUniform2fv
#define glUniform3f budo_glUniform3f
#define glUniform3fv budo_glUniform3fv
#define glUniform4f budo_glUniform4f
#define glUniform4fv budo_glUniform4fv
#define glUniformMatrix3fv budo_glUniformMatrix3fv
#define glUniformMatrix4fv budo_glUniformMatrix4fv
#define glUseProgram budo_glUseProgram
#define glVertexAttribDivisor budo_glVertexAttribDivisor
#define glVertexAttribPointer budo_glVertexAttribPointer

#endif 

#ifdef __cplusplus
}
#endif

#endif