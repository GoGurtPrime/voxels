/**
 * @file gl_renderer.cpp
 * @brief Real OpenGL 3.3 renderer implementation and temporary test-scene rendering.
 */

#include "voxels/graphics/gl_renderer.hpp"

#include <array>
#include <cmath>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "voxels/core/logger.hpp"

namespace voxels::graphics {

namespace {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
};

std::vector<Vertex> BuildCubeVertices() {
    static const std::array<Vertex, 24> vertices = {{
        {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f,-1.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f,-1.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f,-1.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f,-1.0f}, {0.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        {{-0.5f, -0.5f,  0.5f}, {0.0f,-1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f,-1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f,-1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f, -0.5f, -0.5f}, {0.0f,-1.0f, 0.0f}, {0.0f, 1.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        {{-0.5f, -0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{-0.5f,  0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}} }};
    return {vertices.begin(), vertices.end()};
}

std::vector<unsigned int> BuildCubeIndices() {
    return {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4,
        8, 9, 10, 10, 11, 8,
        12, 13, 14, 14, 15, 12,
        16, 17, 18, 18, 19, 16,
        20, 21, 22, 22, 23, 20,
    };
}

} // namespace

GLRenderer::~GLRenderer() {
    Shutdown();
}

bool GLRenderer::Initialize() {
    if (m_initialized) {
        return true;
    }

    if (glGetString == nullptr || glGetString(GL_VERSION) == nullptr) {
        return false;
    }

    const GLubyte* vendor = glGetString(GL_VENDOR);
    const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version = glGetString(GL_VERSION);
    voxels::Logger logger;
    logger.Info(std::string("OpenGL vendor: ") + reinterpret_cast<const char*>(vendor));
    logger.Info(std::string("OpenGL renderer: ") + reinterpret_cast<const char*>(renderer));
    logger.Info(std::string("OpenGL version: ") + reinterpret_cast<const char*>(version));

    const char* vertexSource = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
uniform mat4 uViewProj;
uniform mat4 uModel;
out vec3 vNormal;
out vec2 vTexCoord;
out vec3 vWorldPos;
void main() {
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(uModel) * aNormal;
    vTexCoord = aTexCoord;
    gl_Position = uViewProj * worldPos;
})";

    const char* fragmentSource = R"(
#version 330 core
in vec3 vNormal;
in vec2 vTexCoord;
in vec3 vWorldPos;
uniform vec3 uCameraPos;
uniform sampler2D uTexture;
out vec4 FragColor;
void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(vec3(-0.6, 1.0, -0.5));
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 baseColor = texture(uTexture, vTexCoord).rgb;
    float fog = clamp((length(vWorldPos - uCameraPos) - 2.0) / 18.0, 0.0, 1.0);
    vec3 lit = baseColor * (0.35 + diff * 0.85);
    vec3 skyTint = vec3(0.55, 0.70, 0.92);
    FragColor = vec4(mix(lit, skyTint, fog), 1.0);
})";

    GLuint vertexShader = 0;
    GLuint fragmentShader = 0;
    if (!CompileShader(GL_VERTEX_SHADER, vertexSource, vertexShader)) {
        return false;
    }
    if (!CompileShader(GL_FRAGMENT_SHADER, fragmentSource, fragmentShader)) {
        glDeleteShader(vertexShader);
        return false;
    }
    if (!LinkProgram(vertexShader, fragmentShader, m_program)) {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    CreateProceduralTexture();
    CreateCubeMesh();
    SetupState();

    m_uniformViewProj = glGetUniformLocation(m_program, "uViewProj");
    m_uniformModel = glGetUniformLocation(m_program, "uModel");
    m_uniformCameraPos = glGetUniformLocation(m_program, "uCameraPos");
    m_uniformTexture = glGetUniformLocation(m_program, "uTexture");

    m_initialized = true;
    return true;
}

void GLRenderer::Shutdown() {
    if (!m_initialized) {
        return;
    }
    if (m_vao != 0) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo != 0) glDeleteBuffers(1, &m_vbo);
    if (m_ebo != 0) glDeleteBuffers(1, &m_ebo);
    if (m_texture != 0) glDeleteTextures(1, &m_texture);
    if (m_program != 0) glDeleteProgram(m_program);
    m_vao = 0; m_vbo = 0; m_ebo = 0; m_texture = 0; m_program = 0;
    m_initialized = false;
}

bool GLRenderer::BeginFrame(const std::array<float, 4>& clearColor) {
    if (!m_initialized) {
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, 1280, 720);
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return true;
}

bool GLRenderer::EndFrame() {
    if (!m_initialized) {
        return false;
    }
    glFlush();
    return true;
}

void GLRenderer::CreateProceduralTexture() {
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    std::vector<std::uint8_t> pixels(16 * 16 * 4, 0);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const bool dark = ((x / 4 + y / 4) % 2) == 0;
            const std::uint8_t shade = dark ? 100U : 180U;
            const int idx = (y * 16 + x) * 4;
            pixels[idx] = shade;
            pixels[idx + 1] = static_cast<std::uint8_t>(shade * 0.8f);
            pixels[idx + 2] = static_cast<std::uint8_t>(shade * 0.6f);
            pixels[idx + 3] = 255U;
        }
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
}

void GLRenderer::CreateCubeMesh() {
    const auto vertices = BuildCubeVertices();
    const auto indices = BuildCubeIndices();

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &m_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)), indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, texCoord)));
}

void GLRenderer::SetupState() {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

bool GLRenderer::CompileShader(GLenum type, const char* sourceCode, GLuint& shaderId) const {
    shaderId = glCreateShader(type);
    glShaderSource(shaderId, 1, &sourceCode, nullptr);
    glCompileShader(shaderId);

    GLint success = 0;
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &success);
    if (success == GL_FALSE) {
        GLint logLength = 0;
        glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> infoLog(static_cast<std::size_t>(logLength) + 1U, '\0');
        glGetShaderInfoLog(shaderId, logLength, nullptr, infoLog.data());
        voxels::Logger logger;
        logger.Error(std::string("Shader compile failed: ") + infoLog.data());
        glDeleteShader(shaderId);
        shaderId = 0;
        return false;
    }
    return true;
}

bool GLRenderer::LinkProgram(GLuint vertexShader, GLuint fragmentShader, GLuint& programId) const {
    programId = glCreateProgram();
    glAttachShader(programId, vertexShader);
    glAttachShader(programId, fragmentShader);
    glLinkProgram(programId);

    GLint status = 0;
    glGetProgramiv(programId, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        GLint logLength = 0;
        glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> infoLog(static_cast<std::size_t>(logLength) + 1U, '\0');
        glGetProgramInfoLog(programId, logLength, nullptr, infoLog.data());
        voxels::Logger logger;
        logger.Error(std::string("Shader link failed: ") + infoLog.data());
        glDeleteProgram(programId);
        programId = 0;
        return false;
    }
    return true;
}

void GLRenderer::ApplyUniforms(const glm::mat4& modelMatrix) const {
    const glm::mat4 view = m_camera.View();
    const glm::mat4 projection = m_camera.Projection();
    const glm::mat4 viewProj = projection * view;
    glUseProgram(m_program);
    glUniformMatrix4fv(m_uniformViewProj, 1, GL_FALSE, &viewProj[0][0]);
    glUniformMatrix4fv(m_uniformModel, 1, GL_FALSE, &modelMatrix[0][0]);
    glUniform3fv(m_uniformCameraPos, 1, glm::value_ptr(m_camera.position));
    glUniform1i(m_uniformTexture, 0);
}

void GLRenderer::RenderTestScene(float elapsedSeconds) {
    if (!m_initialized) {
        return;
    }

    glBindVertexArray(m_vao);
    glUseProgram(m_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);

    static const float kSpacing = 1.25f;
    const glm::mat4 view = m_camera.View();
    const glm::mat4 projection = m_camera.Projection();
    const glm::mat4 viewProjection = projection * view;
    glUniformMatrix4fv(m_uniformViewProj, 1, GL_FALSE, &viewProjection[0][0]);
    glUniform3fv(m_uniformCameraPos, 1, glm::value_ptr(m_camera.position));
    glUniform1i(m_uniformTexture, 0);

    for (int z = -2; z <= 2; ++z) {
        for (int x = -2; x <= 2; ++x) {
            const glm::vec3 basePosition{x * kSpacing, 0.0f, z * kSpacing};
            const float height = 0.35f + 0.18f * std::sin((x + elapsedSeconds) * 1.7f + z * 0.8f);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), basePosition + glm::vec3(0.0f, height, 0.0f));
            model = glm::scale(model, glm::vec3(0.75f, 0.75f + height, 0.75f));
            glUniformMatrix4fv(m_uniformModel, 1, GL_FALSE, &model[0][0]);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        }
    }
}

} // namespace voxels::graphics
