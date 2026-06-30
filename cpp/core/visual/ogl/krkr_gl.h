#pragma once

#include "ogl_common.h"

#include <functional>

namespace krkr {
namespace gl {

void BindTexture2D(GLuint textureId);
void BindTexture2DN(unsigned int slot, GLuint textureId);
void ActiveTexture(GLenum textureUnit);
void DeleteTexture(GLuint textureId);
void UseProgram(GLuint program);
void EnableVertexAttribs(unsigned int flags);
void BlendResetToCache();
void InvalidateStateCache();
void OnRendererRecreated(std::function<void()> callback);
void FireRendererRecreated();

} // namespace gl
} // namespace krkr
