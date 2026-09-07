/**
 * @file web_ui_compositor.cpp
 * @brief Implements bounded browser-paint frame handoff.
 */

#include "voxels/ui/web_ui_compositor.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace voxels {
namespace {

bool IsValidSurface(const int width, const int height, const std::size_t byteCount, const std::size_t maximumBytes) {
    if (width <= 0 || height <= 0) return false;
    const std::size_t safeWidth = static_cast<std::size_t>(width);
    const std::size_t safeHeight = static_cast<std::size_t>(height);
    if (safeWidth > std::numeric_limits<std::size_t>::max() / 4U) return false;
    const std::size_t rowBytes = safeWidth * 4U;
    if (safeHeight > std::numeric_limits<std::size_t>::max() / rowBytes) return false;
    const std::size_t requiredBytes = rowBytes * safeHeight;
    return requiredBytes == byteCount && requiredBytes <= maximumBytes;
}

std::optional<WebUiDirtyRect> ClipRect(const WebUiDirtyRect rect, const int width, const int height) {
    const int left = std::clamp(rect.x, 0, width);
    const int top = std::clamp(rect.y, 0, height);
    const long long rightUnclamped = static_cast<long long>(rect.x) + static_cast<long long>(rect.width);
    const long long bottomUnclamped = static_cast<long long>(rect.y) + static_cast<long long>(rect.height);
    const int right = static_cast<int>(std::clamp(rightUnclamped, 0LL, static_cast<long long>(width)));
    const int bottom = static_cast<int>(std::clamp(bottomUnclamped, 0LL, static_cast<long long>(height)));
    if (right <= left || bottom <= top) return std::nullopt;
    return WebUiDirtyRect{left, top, right - left, bottom - top};
}

} // namespace

WebUiSurfaceMetrics ResolveWebUiSurfaceMetrics(const int logicalWidth, const int logicalHeight,
                                               const int drawableWidth, const int drawableHeight) noexcept {
    WebUiSurfaceMetrics metrics{};
    metrics.viewWidth = std::max(logicalWidth, 1);
    metrics.viewHeight = std::max(logicalHeight, 1);
    const float scaleX = static_cast<float>(std::max(drawableWidth, 1)) /
                         static_cast<float>(metrics.viewWidth);
    const float scaleY = static_cast<float>(std::max(drawableHeight, 1)) /
                         static_cast<float>(metrics.viewHeight);
    if (std::isfinite(scaleX) && std::isfinite(scaleY) && scaleX >= 0.5f && scaleX <= 8.0f &&
        std::abs(scaleX - scaleY) <= 0.05f) {
        metrics.deviceScaleFactor = (scaleX + scaleY) * 0.5f;
    }
    return metrics;
}

WebUiFrameQueue::WebUiFrameQueue(const std::size_t maximumPaintBytes)
    : m_maximumPaintBytes(maximumPaintBytes) {}

bool WebUiFrameQueue::Submit(const int width, const int height, const std::span<const std::uint8_t> bgraPixels,
                             const std::span<const WebUiDirtyRect> dirtyRects) {
    if (!IsValidSurface(width, height, bgraPixels.size(), m_maximumPaintBytes)) return false;

    WebUiPaintFrame frame;
    frame.width = width;
    frame.height = height;
    frame.bgraPixels.assign(bgraPixels.begin(), bgraPixels.end());
    frame.dirtyRects.reserve(dirtyRects.size());
    for (const WebUiDirtyRect& dirtyRect : dirtyRects) {
        if (const auto clipped = ClipRect(dirtyRect, width, height); clipped.has_value()) {
            frame.dirtyRects.push_back(*clipped);
        }
    }
    if (frame.dirtyRects.empty()) frame.dirtyRects.push_back({0, 0, width, height});

    std::scoped_lock lock(m_mutex);
    if (m_pendingFrame.has_value()) ++m_droppedFrameCount;
    m_pendingFrame = std::move(frame);
    return true;
}

std::optional<WebUiPaintFrame> WebUiFrameQueue::ConsumeLatest() {
    std::scoped_lock lock(m_mutex);
    return std::exchange(m_pendingFrame, std::nullopt);
}

std::size_t WebUiFrameQueue::RetainedPaintBytes() const noexcept {
    std::scoped_lock lock(m_mutex);
    return m_pendingFrame.has_value() ? m_pendingFrame->bgraPixels.size() : 0U;
}

std::size_t WebUiFrameQueue::DroppedFrameCount() const noexcept {
    std::scoped_lock lock(m_mutex);
    return m_droppedFrameCount;
}

WebUiOpenGLCompositor::~WebUiOpenGLCompositor() {
    Shutdown();
}

bool WebUiOpenGLCompositor::Initialize() {
    if (m_program != 0U) return true;
    if (glCreateShader == nullptr || glGenTextures == nullptr) return false;
    return CreateProgram();
}

void WebUiOpenGLCompositor::Shutdown() noexcept {
    if (glDeleteVertexArrays != nullptr && m_vertexArray != 0U) glDeleteVertexArrays(1, &m_vertexArray);
    if (glDeleteTextures != nullptr && m_texture != 0U) glDeleteTextures(1, &m_texture);
    if (glDeleteProgram != nullptr && m_program != 0U) glDeleteProgram(m_program);
    m_vertexArray = 0U;
    m_texture = 0U;
    m_program = 0U;
    m_textureWidth = 0;
    m_textureHeight = 0;
    m_lastUploadCount = 0U;
    m_hasUploadedFrame = false;
}

bool WebUiOpenGLCompositor::UploadAndComposite(WebUiFrameQueue& queue) {
    m_lastUploadCount = 0U;
    if (!Initialize()) return false;
    const auto frame = queue.ConsumeLatest();
    if (frame.has_value()) {
        if (!UploadFrame(*frame)) return false;
        m_hasUploadedFrame = true;
    }
    // CEF only repaints when its surface changes. Keep compositing the last complete texture
    // on frames without a new paint instead of letting the menu flash for one frame.
    if (!m_hasUploadedFrame) return true;

    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(m_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glBindVertexArray(m_vertexArray);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (depthEnabled == GL_TRUE) glEnable(GL_DEPTH_TEST);
    if (cullEnabled == GL_TRUE) glEnable(GL_CULL_FACE);
    return true;
}

bool WebUiOpenGLCompositor::CreateProgram() {
    constexpr const char* vertexSource = R"(#version 330 core
out vec2 uv;
const vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
void main() { gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0); uv = (positions[gl_VertexID] + 1.0) * 0.5; }
)";
    constexpr const char* fragmentSource = R"(#version 330 core
in vec2 uv;
uniform sampler2D uiTexture;
out vec4 color;
void main() { color = texture(uiTexture, vec2(uv.x, 1.0 - uv.y)); }
)";
    const auto compile = [](const GLenum type, const char* source) {
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint compiled = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled == GL_TRUE) return shader;
        glDeleteShader(shader);
        return 0U;
    };
    const GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertex == 0U || fragment == 0U) {
        if (vertex != 0U) glDeleteShader(vertex);
        if (fragment != 0U) glDeleteShader(fragment);
        return false;
    }
    m_program = glCreateProgram();
    glAttachShader(m_program, vertex);
    glAttachShader(m_program, fragment);
    glLinkProgram(m_program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) { Shutdown(); return false; }
    glUseProgram(m_program);
    glUniform1i(glGetUniformLocation(m_program, "uiTexture"), 0);
    glGenTextures(1, &m_texture);
    glGenVertexArrays(1, &m_vertexArray);
    return m_texture != 0U && m_vertexArray != 0U;
}

bool WebUiOpenGLCompositor::UploadFrame(const WebUiPaintFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0) return false;
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (frame.width != m_textureWidth || frame.height != m_textureHeight) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, frame.width, frame.height, 0, GL_BGRA, GL_UNSIGNED_BYTE,
                     frame.bgraPixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_textureWidth = frame.width;
        m_textureHeight = frame.height;
        m_lastUploadCount = 1U;
        return true;
    }
    // CEF's buffer contains the full current surface. Uploading it once avoids stale pixels
    // when Chromium splits an animated control's repaint into more dirty rectangles than a
    // partial-update budget would process in one game frame.
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.width, frame.height, GL_BGRA, GL_UNSIGNED_BYTE,
                    frame.bgraPixels.data());
    m_lastUploadCount = 1U;
    return true;
}

} // namespace voxels