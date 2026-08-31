/**
 * @file gpu_model_preview.cpp
 * @brief OpenGL framebuffer preview for textured sub-voxel models.
 */

#include "gpu_model_preview.hpp"

#include <cstddef>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "voxels/render/chunk_vertex.hpp"
#include "voxels/render/model_registry.hpp"

namespace voxels::editor {
namespace {

GLuint Compile(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_FALSE) { glDeleteShader(shader); return 0; }
    return shader;
}

} // namespace

GpuModelPreview::~GpuModelPreview() { Shutdown(); }

bool GpuModelPreview::Initialize() {
    if (m_program != 0) return true;
    constexpr char vertexSource[] = "#version 330 core\nlayout(location=0)in vec3 p;layout(location=1)in vec2 uv;uniform mat4 mvp;out vec2 t;void main(){gl_Position=mvp*vec4(p/16.0,1);t=uv;}";
    constexpr char fragmentSource[] = "#version 330 core\nin vec2 t;uniform sampler2D tex;out vec4 c;void main(){c=texture(tex,t);if(c.a<0.05)discard;}";
    const GLuint vertex = Compile(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragment = Compile(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertex == 0 || fragment == 0) { if (vertex != 0) glDeleteShader(vertex); if (fragment != 0) glDeleteShader(fragment); return false; }
    m_program = glCreateProgram(); glAttachShader(m_program, vertex); glAttachShader(m_program, fragment); glLinkProgram(m_program); glDeleteShader(vertex); glDeleteShader(fragment);
    GLint linked = GL_FALSE; glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) { Shutdown(); return false; }
    glGenVertexArrays(1, &m_vao); glGenBuffers(1, &m_vbo); glGenBuffers(1, &m_ibo); glGenTextures(1, &m_sourceTexture);
    glBindVertexArray(m_vao); glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(graphics::ChunkVertex), reinterpret_cast<void*>(offsetof(graphics::ChunkVertex, x)));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(graphics::ChunkVertex), reinterpret_cast<void*>(offsetof(graphics::ChunkVertex, u)));
    glBindVertexArray(0);
    return true;
}

void GpuModelPreview::Resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    m_width = width; m_height = height;
    if (m_framebuffer == 0) { glGenFramebuffers(1, &m_framebuffer); glGenTextures(1, &m_colorTexture); glGenRenderbuffers(1, &m_depthBuffer); }
    glBindTexture(GL_TEXTURE_2D, m_colorTexture); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthBuffer); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTexture, 0); glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depthBuffer); glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool GpuModelPreview::Render(const VoxelModel& model, const ImageData& texture, int width, int height) {
    if (glCreateShader == nullptr || width < 1 || height < 1 || texture.pixels.empty() || !Initialize()) return false;
    Resize(width, height);
    const graphics::BakedModelMesh mesh = graphics::ModelRegistry::BakeModel(model);
    glBindTexture(GL_TEXTURE_2D, m_sourceTexture); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture.width, texture.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture.pixels.data()); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer); glViewport(0, 0, width, height); glEnable(GL_DEPTH_TEST); glClearColor(0.08F, 0.11F, 0.12F, 1.0F); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const glm::mat4 projection = glm::perspective(glm::radians(38.0F), static_cast<float>(width) / static_cast<float>(height), 0.1F, 20.0F);
    const glm::mat4 view = glm::lookAt(glm::vec3(2.2F, 1.8F, 2.2F), glm::vec3(0.5F, 0.5F, 0.5F), glm::vec3(0.0F, 1.0F, 0.0F));
    glUseProgram(m_program); glUniformMatrix4fv(glGetUniformLocation(m_program, "mvp"), 1, GL_FALSE, glm::value_ptr(projection * view)); glUniform1i(glGetUniformLocation(m_program, "tex"), 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m_sourceTexture); glBindVertexArray(m_vao); glBindBuffer(GL_ARRAY_BUFFER, m_vbo); glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(graphics::ChunkVertex)), mesh.vertices.data(), GL_DYNAMIC_DRAW); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)), mesh.indices.data(), GL_DYNAMIC_DRAW); glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr); glBindVertexArray(0); glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void GpuModelPreview::Shutdown() {
    if (m_depthBuffer != 0) glDeleteRenderbuffers(1, &m_depthBuffer); if (m_colorTexture != 0) glDeleteTextures(1, &m_colorTexture); if (m_sourceTexture != 0) glDeleteTextures(1, &m_sourceTexture); if (m_framebuffer != 0) glDeleteFramebuffers(1, &m_framebuffer); if (m_ibo != 0) glDeleteBuffers(1, &m_ibo); if (m_vbo != 0) glDeleteBuffers(1, &m_vbo); if (m_vao != 0) glDeleteVertexArrays(1, &m_vao); if (m_program != 0) glDeleteProgram(m_program);
    m_program = m_vao = m_vbo = m_ibo = m_framebuffer = m_colorTexture = m_depthBuffer = m_sourceTexture = 0; m_width = m_height = 0;
}

} // namespace voxels::editor