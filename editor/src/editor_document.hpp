/**
 * @file editor_document.hpp
 * @brief Testable document and project operations for the voxel content editor.
 *
 * @details Owns authoring-time VMDL mutations, durable file operations, and pack invocation.
 *          The ImGui shell is deliberately a client of this class so editing behavior can be
 *          verified without a desktop graphics context.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "voxels/assets/asset_bundler.hpp"
#include "voxels/assets/vmdl_codec.hpp"
#include "voxels/assets/asset_manager.hpp"

namespace voxels::editor {

struct VoxelCoordinate {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t z = 0;
};

enum class Axis { X, Y, Z };

class EditorDocument {
public:
    static constexpr std::size_t kUndoLimit = 128;

    EditorDocument();
    explicit EditorDocument(VoxelModel model);

    [[nodiscard]] const VoxelModel& Model() const noexcept { return m_model; }
    [[nodiscard]] VoxelModel& Model() noexcept { return m_model; }
    [[nodiscard]] bool IsDirty() const noexcept { return m_dirty; }
    [[nodiscard]] bool CanUndo() const noexcept { return !m_undo.empty(); }
    [[nodiscard]] bool CanRedo() const noexcept { return !m_redo.empty(); }
    [[nodiscard]] const std::vector<VoxelCoordinate>& Selection() const noexcept { return m_selection; }
    [[nodiscard]] bool IsSelected(VoxelCoordinate coordinate) const noexcept;

    bool SetVoxel(VoxelCoordinate coordinate, std::uint16_t paletteIndex);
    bool Fill(VoxelCoordinate coordinate, std::uint16_t paletteIndex);
    bool MoveSelection(const std::vector<VoxelCoordinate>& selection, int offsetX, int offsetY, int offsetZ);
    bool Mirror(Axis axis);
    bool Rotate90(Axis axis);
    bool RemovePaletteEntry(std::size_t index);
    bool UpdatePaletteEntry(std::size_t index, VoxelPaletteEntry entry);
    bool SetPivot(std::array<float, 3> pivot);
    bool SetBounds(std::array<float, 3> minimum, std::array<float, 3> maximum);
    bool Select(VoxelCoordinate coordinate, bool additive);
    bool SelectBox(VoxelCoordinate minimum, VoxelCoordinate maximum, bool additive);
    void ClearSelection() noexcept { m_selection.clear(); }
    bool MoveSelected(int offsetX, int offsetY, int offsetZ);
    [[nodiscard]] std::optional<std::uint16_t> PaletteAt(VoxelCoordinate coordinate) const noexcept;
    void AddPaletteEntry(VoxelPaletteEntry entry);
    bool Undo();
    bool Redo();
    void MarkSaved() noexcept { m_dirty = false; }

    [[nodiscard]] bool Save(const std::filesystem::path& path, std::string& error);
    [[nodiscard]] bool Load(const std::filesystem::path& path, std::string& error);
    [[nodiscard]] bool WriteRecovery(const std::filesystem::path& path, std::string& error) const;
    [[nodiscard]] bool ExportObj(const std::filesystem::path& path, std::string& error) const;

private:
    [[nodiscard]] bool IsInBounds(VoxelCoordinate coordinate) const noexcept;
    [[nodiscard]] std::size_t Index(VoxelCoordinate coordinate) const noexcept;
    void PushUndo();
    void CommitMutation();

    VoxelModel m_model;
    std::vector<VoxelModel> m_undo;
    std::vector<VoxelModel> m_redo;
    std::vector<VoxelCoordinate> m_selection;
    bool m_dirty = false;
};

class EditorProject {
public:
    explicit EditorProject(std::filesystem::path root = {});

    [[nodiscard]] const std::filesystem::path& Root() const noexcept { return m_root; }
    [[nodiscard]] std::vector<std::filesystem::path> ListAssets() const;
    [[nodiscard]] bool CreateLayout(std::string& error) const;
    [[nodiscard]] bool BuildPack(const std::filesystem::path& output, AssetBundleReport& report, std::string& error) const;
    [[nodiscard]] bool CreateBlockTexture(const std::string& textureId, const ImageData& image, std::string& error) const;
    [[nodiscard]] bool ImportBlockTexture(const std::filesystem::path& source, const std::string& textureId, std::string& error) const;
    [[nodiscard]] bool UpsertModelBlock(const std::string& id, const std::string& displayName, const std::string& modelId,
                                        const std::string& textureId, float hardness, std::string& error) const;

private:
    std::filesystem::path m_root;
};

class TextureDocument {
public:
    TextureDocument(int width = 16, int height = 16);
    [[nodiscard]] const ImageData& Image() const noexcept { return m_image; }
    [[nodiscard]] bool SetPixel(int x, int y, std::array<std::uint8_t, 4> color) noexcept;
    [[nodiscard]] bool Save(const std::filesystem::path& path) const;
private:
    ImageData m_image;
};

} // namespace voxels::editor