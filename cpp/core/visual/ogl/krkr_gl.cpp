#include "krkr_gl.h"

#include <algorithm>
#include <vector>

namespace {

constexpr int kMaxTextureUnits = 16;
constexpr int kMaxVertexAttribs = 16;

GLenum sActiveTextureUnit = GL_TEXTURE0;
GLuint sBoundTextures[kMaxTextureUnits] = {};
GLuint sCurrentProgram = 0;
unsigned int sEnabledVertexAttribs = 0;
std::vector<std::function<void()>> sRendererRecreatedCallbacks;

} // namespace

namespace krkr {
namespace gl {

void BindTexture2D(GLuint textureId) {
    BindTexture2DN(0, textureId);
}

void BindTexture2DN(unsigned int slot, GLuint textureId) {
    const GLenum unit = GL_TEXTURE0 + slot;
    sActiveTextureUnit = unit;
    glActiveTexture(unit);
    if(slot < kMaxTextureUnits)
        sBoundTextures[slot] = textureId;
    glBindTexture(GL_TEXTURE_2D, textureId);
}

void ActiveTexture(GLenum textureUnit) {
    sActiveTextureUnit = textureUnit;
    glActiveTexture(textureUnit);
}

void DeleteTexture(GLuint textureId) {
    for(GLuint &boundTexture : sBoundTextures) {
        if(boundTexture == textureId)
            boundTexture = 0;
    }
    glDeleteTextures(1, &textureId);
}

void UseProgram(GLuint program) {
    sCurrentProgram = program;
    glUseProgram(program);
}

void EnableVertexAttribs(unsigned int flags) {
    for(int index = 0; index < kMaxVertexAttribs; ++index) {
        const unsigned int bit = 1u << index;
        if(flags & bit) {
            glEnableVertexAttribArray(index);
        } else if(sEnabledVertexAttribs & bit) {
            glDisableVertexAttribArray(index);
        }
    }
    sEnabledVertexAttribs = flags;
}

void BlendResetToCache() {}

void InvalidateStateCache() {
    sActiveTextureUnit = GL_TEXTURE0;
    std::fill(std::begin(sBoundTextures), std::end(sBoundTextures), 0);
    sCurrentProgram = 0;
    sEnabledVertexAttribs = 0;
}

void OnRendererRecreated(std::function<void()> callback) {
    sRendererRecreatedCallbacks.push_back(std::move(callback));
}

void FireRendererRecreated() {
    InvalidateStateCache();
    for(auto &callback : sRendererRecreatedCallbacks)
        callback();
}

} // namespace gl
} // namespace krkr
