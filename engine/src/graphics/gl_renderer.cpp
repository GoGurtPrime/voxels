/**
 * @file gl_renderer.cpp
 * @brief Real OpenGL 3.3 renderer implementation and block-textured test-scene rendering.
 */

#include "voxels/graphics/gl_renderer.hpp"

#include <array>
#include <cmath>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "voxels/core/logger.hpp"
#include "voxels/render/texture_forge.hpp"

namespace voxels::graphics {

namespace {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    float faceIndex;
};

std::vector<Vertex> BuildCubeVertices() {
    static const std::array<Vertex, 24> vertices = {{
        // Face 0: South (+Z)
        {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, 0.0f},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, 0.0f},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, 0.0f},
        {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, 0.0f},

        // Face 1: North (-Z)
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f,-1.0f}, {0.0f, 1.0f}, 1.0f},
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f,-1.0f}, {1.0f, 1.0f}, 1.0f},
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f,-1.0f}, {1.0f, 0.0f}, 1.0f},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f,-1.0f}, {0.0f, 0.0f}, 1.0f},

        // Face 2: Top (+Y)
        {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, 2.0f},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, 2.0f},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}, 2.0f},
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, 2.0f},

        // Face 3: Bottom (-Y)
        {{-0.5f, -0.5f, -0.5f}, {0.0f,-1.0f, 0.0f}, {0.0f, 1.0f}, 3.0f},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f,-1.0f, 0.0f}, {1.0f, 1.0f}, 3.0f},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f,-1.0f, 0.0f}, {1.0f, 0.0f}, 3.0f},
        {{-0.5f, -0.5f,  0.5f}, {0.0f,-1.0f, 0.0f}, {0.0f, 0.0f}, 3.0f},

        // Face 4: East (+X)
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, 4.0f},
        {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}, 4.0f},
        {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, 4.0f},
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, 4.0f},

        // Face 5: West (-X)
        {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, 5.0f},
        {{-0.5f, -0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}, 5.0f},
        {{-0.5f,  0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, 5.0f},
        {{-0.5f,  0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, 5.0f}
    }};
    return {vertices.begin(), vertices.end()};
}

std::vector<unsigned int> BuildCubeIndices() {
    return {
        0, 1, 2, 2, 3, 0,       // South
        4, 5, 6, 6, 7, 4,       // North
        8, 9, 10, 10, 11, 8,    // Top
        12, 13, 14, 14, 15, 12, // Bottom
        16, 17, 18, 18, 19, 16, // East
        20, 21, 22, 22, 23, 20  // West
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
layout(location = 3) in float aFaceIndex;

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform vec4 uFaceLayers;  // [0]=South, [1]=North, [2]=Top, [3]=Bottom
uniform vec2 uFaceLayers2; // [0]=East, [1]=West

out vec3 vNormal;
out vec2 vTexCoord;
out float vLayer;
out vec3 vWorldPos;

void main() {
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(uModel) * aNormal;
    vTexCoord = aTexCoord;

    int face = int(aFaceIndex + 0.5);
    float layer = 0.0;
    if (face == 0) layer = uFaceLayers[0];
    else if (face == 1) layer = uFaceLayers[1];
    else if (face == 2) layer = uFaceLayers[2];
    else if (face == 3) layer = uFaceLayers[3];
    else if (face == 4) layer = uFaceLayers2[0];
    else if (face == 5) layer = uFaceLayers2[1];
    vLayer = layer;

    gl_Position = uViewProj * worldPos;
})";

    const char* fragmentSource = R"(
#version 330 core
in vec3 vNormal;
in vec2 vTexCoord;
in float vLayer;
in vec3 vWorldPos;

uniform vec3 uCameraPos;
uniform sampler2DArray uTextureAtlas;
uniform vec4 uTintColor;

out vec4 FragColor;

void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(vec3(-0.6, 1.0, -0.5));
    float diff = max(dot(normal, lightDir), 0.0);
    vec4 texColor = texture(uTextureAtlas, vec3(vTexCoord, vLayer));
    if (texColor.a < 0.05) {
        discard;
    }
    vec3 baseColor = texColor.rgb;
    if (uTintColor.a > 0.0) {
        baseColor *= uTintColor.rgb;
    }
    float fog = clamp((length(vWorldPos - uCameraPos) - 2.0) / 24.0, 0.0, 1.0);
    vec3 lit = baseColor * (0.35 + diff * 0.85);
    vec3 skyTint = vec3(0.55, 0.70, 0.92);
    FragColor = vec4(mix(lit, skyTint, fog), texColor.a);
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

    if (m_atlas == nullptr) {
        CreateDefaultAtlas();
    }
    CreateCubeMesh();
    SetupState();

    m_uniformViewProj = glGetUniformLocation(m_program, "uViewProj");
    m_uniformModel = glGetUniformLocation(m_program, "uModel");
    m_uniformCameraPos = glGetUniformLocation(m_program, "uCameraPos");
    m_uniformTexture = glGetUniformLocation(m_program, "uTextureAtlas");
    m_uniformFaceLayers = glGetUniformLocation(m_program, "uFaceLayers");
    m_uniformTintColor = glGetUniformLocation(m_program, "uTintColor");

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
    if (m_program != 0) glDeleteProgram(m_program);
    m_vao = 0; m_vbo = 0; m_ebo = 0; m_program = 0;
    if (m_ownedAtlas) {
        m_ownedAtlas->Shutdown();
        m_ownedAtlas.reset();
    }
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

void GLRenderer::CreateDefaultAtlas() {
    m_ownedAtlas = std::make_unique<TextureAtlas>(16, 16);
    for (const auto& name : TextureForge::GetLaunchTextureNames()) {
        m_ownedAtlas->RegisterTexture("blocks/" + name, TextureForge::GenerateTexture("blocks/" + name));
    }
    m_ownedAtlas->BuildGLTexture();
    m_atlas = m_ownedAtlas.get();
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
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, faceIndex)));
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
    if (!m_initialized || m_atlas == nullptr) {
        return;
    }

    glBindVertexArray(m_vao);
    glUseProgram(m_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_atlas->GetTextureHandle());

    static const float kSpacing = 1.35f;
    const glm::mat4 view = m_camera.View();
    const glm::mat4 projection = m_camera.Projection();
    const glm::mat4 viewProjection = projection * view;
    glUniformMatrix4fv(m_uniformViewProj, 1, GL_FALSE, &viewProjection[0][0]);
    glUniform3fv(m_uniformCameraPos, 1, glm::value_ptr(m_camera.position));
    glUniform1i(m_uniformTexture, 0);

    const GLint locFaceLayers2 = glGetUniformLocation(m_program, "uFaceLayers2");

    // Grid of launch blocks to showcase in the test scene
    struct BlockSample {
        float south, north, top, bottom, east, west;
        glm::vec4 tint;
    };

    auto GetLayer = [this](std::string_view name) -> float {
        return static_cast<float>(m_atlas->LayerFor(name));
    };

    const float lStone = GetLayer("blocks/stone");
    const float lDirt = GetLayer("blocks/dirt");
    const float lGrassTop = GetLayer("blocks/grass_top");
    const float lGrassSide = GetLayer("blocks/grass_side");
    const float lSand = GetLayer("blocks/sand");
    const float lGravel = GetLayer("blocks/gravel");
    const float lCoal = GetLayer("blocks/coal_ore");
    const float lIron = GetLayer("blocks/iron_ore");
    const float lLogTop = GetLayer("blocks/wood_log_top");
    const float lLogSide = GetLayer("blocks/wood_log_side");
    const float lLeaves = GetLayer("blocks/leaves");
    const float lPlanks = GetLayer("blocks/planks");
    const float lGlass = GetLayer("blocks/glass");
    const float lBedrock = GetLayer("blocks/bedrock");
    const float lWater = GetLayer("blocks/water");

    const std::array<BlockSample, 13> samples = {{
        // 0: Grass (multi-textured top/side/bottom with tint)
        {lGrassSide, lGrassSide, lGrassTop, lDirt, lGrassSide, lGrassSide, glm::vec4(0.45f, 0.75f, 0.35f, 1.0f)},
        // 1: Stone
        {lStone, lStone, lStone, lStone, lStone, lStone, glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)},
        // 2: Dirt
        {lDirt, lDirt, lDirt, lDirt, lDirt, lDirt, glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)},
        // 3: Wood Log (multi-textured rings on top/bottom, bark on sides)
        {lLogSide, lLogSide, lLogTop, lLogTop, lLogSide, lLogSide, glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)},
        // 4: Planks
        {lPlanks, lPlanks, lPlanks, lPlanks, lPlanks, lPlanks, glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)},
        // 5: Coal Ore
        {lCoal, lCoal, lCoal, lCoal, lCoal, lCoal, glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)},
        // 6: Iron Ore
        {lIron, lIron, lIron, lIron, lIron, lIron, glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)},
        // 7: Sand
        {lSand, lSand, lSand, lSand, lSand, lSand, glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)},
        // 8: Gravel
        {lGravel, lGravel, lGravel, lGravel, lGravel, lGravel, glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)},
        // 9: Leaves (tinted green)
        {lLeaves, lLeaves, lLeaves, lLeaves, lLeaves, lLeaves, glm::vec4(0.38f, 0.68f, 0.28f, 1.0f)},
        // 10: Glass (translucent)
        {lGlass, lGlass, lGlass, lGlass, lGlass, lGlass, glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)},
        // 11: Bedrock
        {lBedrock, lBedrock, lBedrock, lBedrock, lBedrock, lBedrock, glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)},
        // 12: Water
        {lWater, lWater, lWater, lWater, lWater, lWater, glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)}
    }};

    int sampleIdx = 0;
    for (int z = -2; z <= 2; ++z) {
        for (int x = -2; x <= 2; ++x) {
            const auto& sample = samples[sampleIdx % samples.size()];
            ++sampleIdx;

            glUniform4f(m_uniformFaceLayers, sample.south, sample.north, sample.top, sample.bottom);
            glUniform2f(locFaceLayers2, sample.east, sample.west);
            glUniform4f(m_uniformTintColor, sample.tint.r, sample.tint.g, sample.tint.b, sample.tint.a);

            const glm::vec3 basePosition{x * kSpacing, 0.0f, z * kSpacing};
            const float height = 0.35f + 0.18f * std::sin((x + elapsedSeconds) * 1.7f + z * 0.8f);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), basePosition + glm::vec3(0.0f, height, 0.0f));
            model = glm::scale(model, glm::vec3(0.85f, 0.85f + height * 0.5f, 0.85f));
            glUniformMatrix4fv(m_uniformModel, 1, GL_FALSE, &model[0][0]);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        }
    }
}

} // namespace voxels::graphics
