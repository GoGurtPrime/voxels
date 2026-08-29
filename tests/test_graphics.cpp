/**
 * @file test_graphics.cpp
 * @brief Unit tests for Work Item 04 graphics RHI contracts and mock backend.
 *
 * @details Validates the CPU-only graphics backend so renderer behavior can be verified without
 *          a native graphics driver, presentation surface, or platform-specific SDK.
 */

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

#include "voxels/graphics/dreamcast/dreamcast_pvr_renderer.hpp"
#include "voxels/graphics/dx12/dx12_renderer.hpp"
#include "voxels/graphics/material.hpp"
#include "voxels/graphics/mesh.hpp"
#include "voxels/graphics/metal/metal_renderer.hpp"
#include "voxels/graphics/mock/mock_renderer.hpp"
#include "voxels/graphics/vulkan/vulkan_renderer.hpp"

namespace {

voxels::graphics::PipelineStatePtr CreateTestPipeline(voxels::graphics::MockRenderer& renderer) {
    const auto vertexShader = renderer.createShader({voxels::graphics::ShaderStage::Vertex});
    const auto fragmentShader = renderer.createShader({voxels::graphics::ShaderStage::Fragment});
    voxels::graphics::PipelineDescriptor descriptor;
    descriptor.vertexLayout.stride = sizeof(voxels::graphics::VoxelVertex);
    descriptor.vertexShader = vertexShader;
    descriptor.fragmentShader = fragmentShader;
    return renderer.createPipelineState(descriptor);
}

} // namespace

TEST_CASE("Renderer.MockBackendInitialization", "[graphics]") {
    voxels::graphics::MockRenderer renderer;
    REQUIRE_FALSE(renderer.isInitialized());
    REQUIRE(renderer.initialize());
    REQUIRE(renderer.capabilities().backend == voxels::graphics::RendererBackend::Mock);
    REQUIRE_FALSE(renderer.capabilities().supportsPresentation);
    REQUIRE(renderer.beginFrame());
    REQUIRE(renderer.isFrameActive());
    REQUIRE(renderer.endFrame());
    REQUIRE_FALSE(renderer.isFrameActive());
    REQUIRE(renderer.present());
    renderer.shutdown();
    REQUIRE_FALSE(renderer.isInitialized());
}

TEST_CASE("Renderer.BufferCreationAndBinding", "[graphics]") {
    voxels::graphics::MockRenderer renderer;
    REQUIRE(renderer.initialize());

    const auto vertexBuffer = renderer.createBuffer(
        {voxels::graphics::BufferType::Vertex, sizeof(voxels::graphics::VoxelVertex) * 4, "vertices"});
    const auto indexBuffer = renderer.createBuffer({voxels::graphics::BufferType::Index, sizeof(std::uint32_t) * 6, "indices"});
    REQUIRE(vertexBuffer != nullptr);
    REQUIRE(indexBuffer != nullptr);
    REQUIRE(vertexBuffer->sizeBytes() == sizeof(voxels::graphics::VoxelVertex) * 4);
    REQUIRE(indexBuffer->sizeBytes() == sizeof(std::uint32_t) * 6);

    const auto pipeline = CreateTestPipeline(renderer);
    REQUIRE(pipeline != nullptr);
    voxels::graphics::CommandBuffer commands;
    commands.bindVertexBuffer(vertexBuffer);
    commands.bindIndexBuffer(indexBuffer);
    commands.bindPipeline(pipeline);
    commands.drawIndexed(6);

    REQUIRE(renderer.beginFrame());
    REQUIRE(renderer.submitCommandBuffer(commands));
    REQUIRE(renderer.endFrame());
    REQUIRE(renderer.drawCalls().size() == 1);
    REQUIRE(renderer.drawCalls().front().vertexBuffer == vertexBuffer);
    REQUIRE(renderer.drawCalls().front().indexBuffer == indexBuffer);
}

TEST_CASE("Renderer.VoxelMeshDrawCall", "[graphics]") {
    voxels::graphics::MockRenderer renderer;
    REQUIRE(renderer.initialize());

    const voxels::graphics::MeshData mesh{
        {{}, {}, {}, {}},
        {0, 1, 2, 2, 3, 0},
    };
    const auto vertexBuffer = renderer.createBuffer(
        {voxels::graphics::BufferType::Vertex, mesh.vertices.size() * sizeof(voxels::graphics::VoxelVertex), "block mesh"});
    const auto indexBuffer = renderer.createBuffer(
        {voxels::graphics::BufferType::Index, mesh.indices.size() * sizeof(std::uint32_t), "block mesh indices"});
    const auto pipeline = CreateTestPipeline(renderer);
    REQUIRE(vertexBuffer != nullptr);
    REQUIRE(indexBuffer != nullptr);
    REQUIRE(pipeline != nullptr);

    voxels::graphics::CommandBuffer commands;
    commands.bindVertexBuffer(vertexBuffer);
    commands.bindIndexBuffer(indexBuffer);
    commands.bindPipeline(pipeline);
    commands.drawIndexed(static_cast<std::uint32_t>(mesh.indices.size()));

    REQUIRE(renderer.beginFrame());
    REQUIRE(renderer.submitCommandBuffer(commands));
    REQUIRE(renderer.endFrame());
    REQUIRE(renderer.drawCalls().size() == 1);
    REQUIRE(renderer.drawCalls().front().vertexCount == 0);
    REQUIRE(renderer.drawCalls().front().indexCount == mesh.indices.size());
}

TEST_CASE("Renderer.MaterialAndBackendStubs", "[graphics]") {
    voxels::graphics::MockRenderer renderer;
    REQUIRE(renderer.initialize());
    const auto atlas = renderer.createTexture({voxels::graphics::TextureType::Texture2DArray,
                                               voxels::graphics::TextureFormat::Rgba8Unorm, 16, 16, 4, "block atlas"});
    const voxels::graphics::MaterialInstance material{atlas, {}};
    REQUIRE(material.blockAtlas != nullptr);
    REQUIRE(material.blockAtlas->descriptor().layers == 4);

    voxels::graphics::VulkanRenderer vulkan;
    voxels::graphics::DirectX12Renderer dx12;
    voxels::graphics::MetalRenderer metal;
    voxels::graphics::DreamcastPVRRenderer dreamcast;
    REQUIRE(vulkan.capabilities().backend == voxels::graphics::RendererBackend::Vulkan);
    REQUIRE(dx12.capabilities().backend == voxels::graphics::RendererBackend::DirectX12);
    REQUIRE(metal.capabilities().backend == voxels::graphics::RendererBackend::Metal);
    REQUIRE(dreamcast.capabilities().backend == voxels::graphics::RendererBackend::DreamcastPVR);
}