#pragma once

/**
 * @file web_ui_compositor.hpp
 * @brief Bounded CPU-side frame handoff for an off-screen web UI compositor.
 *
 * @details The browser render process submits dirty BGRA regions through this type; the main
 *          render thread consumes the newest complete frame and owns all GPU upload work.
 *          This preserves ADR-008's thread-affinity rule while bounding retained paint memory.
 */

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

#include <glad/glad.h>

namespace voxels {

/// Pixel-space rectangle describing a changed region of a browser frame.
struct WebUiDirtyRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

/// Complete premultiplied-BGRA browser surface consumed only by the render thread.
struct WebUiPaintFrame {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> bgraPixels;
    std::vector<WebUiDirtyRect> dirtyRects;
};

/// Thread-safe newest-frame queue with a strict retained-paint-byte cap.
class WebUiFrameQueue final {
public:
    static constexpr std::size_t kDefaultMaximumPaintBytes = 4U * 1024U * 1024U;

    explicit WebUiFrameQueue(std::size_t maximumPaintBytes = kDefaultMaximumPaintBytes);

    /// Copies and clips CEF-style dirty rectangles from a tightly packed BGRA source surface.
    /// Invalid, oversized, or malformed paint is rejected without changing the pending frame.
    [[nodiscard]] bool Submit(int width, int height, std::span<const std::uint8_t> bgraPixels,
                              std::span<const WebUiDirtyRect> dirtyRects);

    /// Returns the newest pending frame and discards any older submitted frame.
    [[nodiscard]] std::optional<WebUiPaintFrame> ConsumeLatest();

    [[nodiscard]] std::size_t RetainedPaintBytes() const noexcept;
    [[nodiscard]] std::size_t DroppedFrameCount() const noexcept;

private:
    std::size_t m_maximumPaintBytes;
    mutable std::mutex m_mutex;
    std::optional<WebUiPaintFrame> m_pendingFrame;
    std::size_t m_droppedFrameCount = 0;
};

/// OpenGL 3.3 texture/quad pass for premultiplied BGRA browser output. All methods are render-thread-only.
class WebUiOpenGLCompositor final {
public:
    WebUiOpenGLCompositor() = default;
    ~WebUiOpenGLCompositor();

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool UploadAndComposite(WebUiFrameQueue& queue);
    [[nodiscard]] std::size_t LastUploadCount() const noexcept { return m_lastUploadCount; }

private:
    [[nodiscard]] bool CreateProgram();
    [[nodiscard]] bool UploadFrame(const WebUiPaintFrame& frame);

    GLuint m_program = 0U;
    GLuint m_texture = 0U;
    GLuint m_vertexArray = 0U;
    int m_textureWidth = 0;
    int m_textureHeight = 0;
    std::size_t m_lastUploadCount = 0;
};

} // namespace voxels