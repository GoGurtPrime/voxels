/**
 * @file gameplay_hud.cpp
 * @brief Implements GL line primitives for core voxel-world interaction feedback.
 */

#define STB_TRUETYPE_IMPLEMENTATION
#include "voxels/render/gameplay_hud.hpp"

#include <array>
#include <fstream>
#include <vector>

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include "voxels/core/paths.hpp"

namespace voxels::graphics {
namespace {
struct Vertex { glm::vec3 position; glm::vec4 color; };
struct TextVertex { glm::vec2 position; glm::vec2 uv; };

GLuint Compile(GLenum stage, const char* source) {
    const GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint valid = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &valid);
    if (valid == GL_FALSE) { glDeleteShader(shader); return 0; }
    return shader;
}

void AddLine(std::vector<Vertex>& vertices, glm::vec3 a, glm::vec3 b, glm::vec4 color) {
    vertices.push_back({a, color}); vertices.push_back({b, color});
}

void AddRect(std::vector<Vertex>& vertices, float left, float bottom, float right, float top, glm::vec4 color) {
    AddLine(vertices, {left, bottom, 0.0f}, {right, bottom, 0.0f}, color);
    AddLine(vertices, {right, bottom, 0.0f}, {right, top, 0.0f}, color);
    AddLine(vertices, {right, top, 0.0f}, {left, top, 0.0f}, color);
    AddLine(vertices, {left, top, 0.0f}, {left, bottom, 0.0f}, color);
}
} // namespace

GameplayHudRenderer::~GameplayHudRenderer() { Shutdown(); }

void GameplayHudRenderer::Render(const Camera& camera, const RaycastHit& target, float breakProgress,
                                 const gameplay::Inventory& inventory, const std::string& selectedItemLabel,
                                 float selectedItemLabelAge, const std::vector<Vec3I>& particleBursts) {
    if (glCreateShader == nullptr) return;
    if (m_program == 0) {
        constexpr const char* vertexSource = "#version 330 core\nlayout(location=0) in vec3 p; layout(location=1) in vec4 c; uniform mat4 vp; uniform bool screen; out vec4 color; void main(){gl_Position=screen?vec4(p,1):vp*vec4(p,1);color=c;}";
        constexpr const char* fragmentSource = "#version 330 core\nin vec4 color; out vec4 outColor; void main(){outColor=color;}";
        const GLuint vertex = Compile(GL_VERTEX_SHADER, vertexSource);
        const GLuint fragment = Compile(GL_FRAGMENT_SHADER, fragmentSource);
        if (vertex == 0 || fragment == 0) { if (vertex) glDeleteShader(vertex); if (fragment) glDeleteShader(fragment); return; }
        m_program = glCreateProgram(); glAttachShader(m_program, vertex); glAttachShader(m_program, fragment); glLinkProgram(m_program);
        glDeleteShader(vertex); glDeleteShader(fragment);
        GLint linked = GL_FALSE; glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
        if (linked == GL_FALSE) { glDeleteProgram(m_program); m_program = 0; return; }
        glGenVertexArrays(1, &m_vao); glGenBuffers(1, &m_vbo);
        glBindVertexArray(m_vao); glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(sizeof(glm::vec3)));

        constexpr const char* textVertexSource = "#version 330 core\nlayout(location=0) in vec2 p; layout(location=1) in vec2 uv; out vec2 t; void main(){gl_Position=vec4(p,0,1);t=uv;}";
        constexpr const char* textFragmentSource = "#version 330 core\nin vec2 t; uniform sampler2D atlas; uniform vec4 color; out vec4 outColor; void main(){outColor=vec4(color.rgb,color.a*texture(atlas,t).r);}";
        const GLuint textVertex = Compile(GL_VERTEX_SHADER, textVertexSource);
        const GLuint textFragment = Compile(GL_FRAGMENT_SHADER, textFragmentSource);
        if (textVertex != 0 && textFragment != 0) {
            m_textProgram = glCreateProgram(); glAttachShader(m_textProgram, textVertex); glAttachShader(m_textProgram, textFragment); glLinkProgram(m_textProgram);
            GLint textLinked = GL_FALSE; glGetProgramiv(m_textProgram, GL_LINK_STATUS, &textLinked);
            if (textLinked == GL_FALSE) { glDeleteProgram(m_textProgram); m_textProgram = 0; }
            glDeleteShader(textVertex); glDeleteShader(textFragment);
        }
        glGenVertexArrays(1, &m_textVao); glGenBuffers(1, &m_textVbo);
        glBindVertexArray(m_textVao); glBindBuffer(GL_ARRAY_BUFFER, m_textVbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), nullptr);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), reinterpret_cast<void*>(sizeof(glm::vec2)));

        std::ifstream font(Paths::AssetsDir() / "fonts" / "AtkinsonHyperlegible-Regular.ttf", std::ios::binary | std::ios::ate);
        if (font.is_open()) {
            const std::streamsize size = font.tellg(); font.seekg(0, std::ios::beg);
            std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
            if (size > 0 && font.read(reinterpret_cast<char*>(bytes.data()), size)) {
                std::array<unsigned char, 512 * 512> atlas{};
                if (stbtt_BakeFontBitmap(bytes.data(), 0, 24.0f, atlas.data(), 512, 512, 32, 96, m_glyphs.data()) > 0) {
                    glGenTextures(1, &m_fontTexture); glBindTexture(GL_TEXTURE_2D, m_fontTexture);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 512, 512, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    m_fontReady = m_textProgram != 0;
                }
            }
        }
    }

    std::vector<Vertex> worldLines;
    if (target.hit) {
        const glm::vec3 min{static_cast<float>(target.blockPosition.x) - 0.003f, static_cast<float>(target.blockPosition.y) - 0.003f, static_cast<float>(target.blockPosition.z) - 0.003f};
        const glm::vec3 max = min + glm::vec3(1.006f);
        constexpr std::array<std::array<int, 2>, 12> edges = {{{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}}};
        const std::array<glm::vec3, 8> points = {{{min.x,min.y,min.z},{max.x,min.y,min.z},{max.x,max.y,min.z},{min.x,max.y,min.z},{min.x,min.y,max.z},{max.x,min.y,max.z},{max.x,max.y,max.z},{min.x,max.y,max.z}}};
        for (const auto& edge : edges) AddLine(worldLines, points[edge[0]], points[edge[1]], {0,0,0,1});
        if (breakProgress > 0.0f) {
            const glm::vec4 crack{0.12f, 0.04f, 0.02f, 0.55f + 0.45f * breakProgress};
            const float y = max.y + 0.004f;
            const int lines = 1 + static_cast<int>(breakProgress * 9.0f);
            for (int i = 0; i < lines; ++i) { const float x = min.x + (static_cast<float>(i) + 0.5f) / lines; AddLine(worldLines, {x,y,min.z}, {x + 0.12f,y,max.z}, crack); }
        }
    }
    for (const Vec3I& burst : particleBursts) {
        const glm::vec3 center{static_cast<float>(burst.x) + 0.5f, static_cast<float>(burst.y) + 0.5f,
                               static_cast<float>(burst.z) + 0.5f};
        const glm::vec4 particleColor{0.78f, 0.56f, 0.18f, 0.9f};
        AddLine(worldLines, center - glm::vec3(0.18f, 0.0f, 0.0f), center + glm::vec3(0.18f, 0.0f, 0.0f), particleColor);
        AddLine(worldLines, center - glm::vec3(0.0f, 0.18f, 0.0f), center + glm::vec3(0.0f, 0.18f, 0.0f), particleColor);
        AddLine(worldLines, center - glm::vec3(0.0f, 0.0f, 0.18f), center + glm::vec3(0.0f, 0.0f, 0.18f), particleColor);
    }
    glUseProgram(m_program); glBindVertexArray(m_vao); glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const glm::mat4 viewProjection = camera.ViewProjection();
    glUniformMatrix4fv(glGetUniformLocation(m_program, "vp"), 1, GL_FALSE, glm::value_ptr(viewProjection));
    glUniform1i(glGetUniformLocation(m_program, "screen"), GL_FALSE);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(worldLines.size() * sizeof(Vertex)), worldLines.data(), GL_DYNAMIC_DRAW);
    glLineWidth(2.0f); glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(worldLines.size()));

    std::vector<Vertex> screenLines;
    const glm::vec4 white{1,1,1,0.92f};
    AddLine(screenLines, {-0.016f,0,0}, {0.016f,0,0}, white); AddLine(screenLines, {0,-0.026f,0}, {0,0.026f,0}, white);
    constexpr float slotWidth = 0.105f, bottom = -0.78f;
    for (int slot = 0; slot < 9; ++slot) {
        const float left = -0.4725f + slot * slotWidth;
        const bool selected = inventory.GetSelectedSlot() == slot;
        AddRect(screenLines, left, bottom, left + 0.095f, bottom + 0.13f, selected ? glm::vec4{1.0f,0.82f,0.2f,1.0f} : glm::vec4{0.08f,0.08f,0.08f,0.9f});
        const auto& stack = inventory.GetSlot(static_cast<std::size_t>(slot));
        if (!stack.IsEmpty()) AddRect(screenLines, left + 0.028f, bottom + 0.035f, left + 0.067f, bottom + 0.095f, {0.32f,0.72f,0.34f,1.0f});
    }
    const auto& held = inventory.GetSelectedStack();
    if (!held.IsEmpty()) AddRect(screenLines, 0.66f, -0.82f, 0.91f, -0.45f, {0.45f,0.85f,0.38f,1.0f});
    glUniform1i(glGetUniformLocation(m_program, "screen"), GL_TRUE);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(screenLines.size() * sizeof(Vertex)), screenLines.data(), GL_DYNAMIC_DRAW);
    glDisable(GL_DEPTH_TEST); glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(screenLines.size())); glEnable(GL_DEPTH_TEST);

    constexpr float kLabelHoldSeconds = 2.0f;
    constexpr float kLabelFadeSeconds = 1.2f;
    if (m_fontReady && !selectedItemLabel.empty() && selectedItemLabelAge < kLabelHoldSeconds + kLabelFadeSeconds) {
        const float alpha = selectedItemLabelAge <= kLabelHoldSeconds ? 1.0f : 1.0f - (selectedItemLabelAge - kLabelHoldSeconds) / kLabelFadeSeconds;
        float width = 0.0f;
        for (const unsigned char character : selectedItemLabel) {
            if (character >= 32 && character < 128) width += m_glyphs[character - 32].xadvance * 0.0025f;
        }
        float cursorX = -width * 0.5f;
        std::vector<TextVertex> textVertices;
        for (const unsigned char character : selectedItemLabel) {
            if (character < 32 || character >= 128) continue;
            stbtt_aligned_quad quad{};
            float pixelX = cursorX / 0.0025f; float pixelY = -0.91f / 0.0025f;
            stbtt_GetBakedQuad(m_glyphs.data(), 512, 512, character - 32, &pixelX, &pixelY, &quad, 1);
            cursorX = pixelX * 0.0025f;
            const float left = quad.x0 * 0.0025f, right = quad.x1 * 0.0025f, glyphBottom = quad.y0 * 0.0025f, top = quad.y1 * 0.0025f;
            textVertices.insert(textVertices.end(), {{ {left, glyphBottom}, {quad.s0, quad.t1} }, {{right, glyphBottom}, {quad.s1, quad.t1}}, {{right, top}, {quad.s1, quad.t0}}, {{left, glyphBottom}, {quad.s0, quad.t1}}, {{right, top}, {quad.s1, quad.t0}}, {{left, top}, {quad.s0, quad.t0}}});
        }
        glUseProgram(m_textProgram); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m_fontTexture);
        glUniform1i(glGetUniformLocation(m_textProgram, "atlas"), 0); glUniform4f(glGetUniformLocation(m_textProgram, "color"), 1.0f, 0.78f, 0.16f, alpha);
        glBindVertexArray(m_textVao); glBindBuffer(GL_ARRAY_BUFFER, m_textVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(textVertices.size() * sizeof(TextVertex)), textVertices.data(), GL_DYNAMIC_DRAW);
        glDisable(GL_DEPTH_TEST); glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(textVertices.size())); glEnable(GL_DEPTH_TEST);
    }
}

void GameplayHudRenderer::Shutdown() {
    if (m_vbo != 0) glDeleteBuffers(1, &m_vbo);
    if (m_vao != 0) glDeleteVertexArrays(1, &m_vao);
    if (m_program != 0) glDeleteProgram(m_program);
    if (m_textVbo != 0) glDeleteBuffers(1, &m_textVbo);
    if (m_textVao != 0) glDeleteVertexArrays(1, &m_textVao);
    if (m_fontTexture != 0) glDeleteTextures(1, &m_fontTexture);
    if (m_textProgram != 0) glDeleteProgram(m_textProgram);
    m_vbo = m_vao = m_program = m_textVbo = m_textVao = m_fontTexture = m_textProgram = 0;
    m_fontReady = false;
}

} // namespace voxels::graphics