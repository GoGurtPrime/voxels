/**
 * @file editor_document.cpp
 * @brief Durable model editing operations shared by the editor UI and tests.
 *
 * @details Implements editor-side mutation semantics over the VMDL representation in
 *          ARCHITECTURE.md section 6.6. File writes use a temporary sibling and rename so
 *          an interrupted save preserves the previously authored model.
 */

#include "editor_document.hpp"

#include <algorithm>
#include <fstream>
#include <queue>
#include <system_error>

#include <nlohmann/json.hpp>
#include "voxels/assets/texture_loader.hpp"
#include "voxels/world/block.hpp"

namespace voxels::editor {
namespace {

[[nodiscard]] bool WriteBytesAtomically(const std::filesystem::path& path, const std::vector<std::byte>& bytes, std::string& error) {
    std::error_code filesystemError;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), filesystemError);
    if (filesystemError) { error = "could not create directory for " + path.string(); return false; }
    const std::filesystem::path temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) { error = "could not open " + temporary.string(); return false; }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) { error = "could not write " + temporary.string(); return false; }
    std::filesystem::rename(temporary, path, filesystemError);
    if (filesystemError) {
        std::filesystem::remove(path, filesystemError);
        filesystemError.clear();
        std::filesystem::rename(temporary, path, filesystemError);
    }
    if (filesystemError) { error = "could not replace " + path.string(); return false; }
    return true;
}

[[nodiscard]] std::size_t IndexFor(const VoxelModel& model, std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    return x + static_cast<std::size_t>(model.gridSize[0]) * (y + static_cast<std::size_t>(model.gridSize[1]) * z);
}

} // namespace

EditorDocument::EditorDocument() : EditorDocument(VoxelModel{}) {}

EditorDocument::EditorDocument(VoxelModel model) : m_model(std::move(model)) {
    if (m_model.voxels.size() != m_model.GridVoxelCount()) m_model.voxels.assign(m_model.GridVoxelCount(), 0);
    if (m_model.palette.empty()) m_model.palette.push_back({255, 255, 255, 255, 0, 0, 0});
    m_model.ComputeBounds();
}

bool EditorDocument::IsInBounds(VoxelCoordinate coordinate) const noexcept {
    return coordinate.x < m_model.gridSize[0] && coordinate.y < m_model.gridSize[1] && coordinate.z < m_model.gridSize[2];
}

std::size_t EditorDocument::Index(VoxelCoordinate coordinate) const noexcept { return IndexFor(m_model, coordinate.x, coordinate.y, coordinate.z); }

void EditorDocument::PushUndo() {
    if (m_undo.size() == kUndoLimit) m_undo.erase(m_undo.begin());
    m_undo.push_back(m_model);
    m_redo.clear();
}

void EditorDocument::CommitMutation() { m_model.ComputeBounds(); m_dirty = true; }

bool EditorDocument::SetVoxel(VoxelCoordinate coordinate, std::uint16_t paletteIndex) {
    if (!IsInBounds(coordinate) || paletteIndex > m_model.palette.size() || m_model.voxels[Index(coordinate)] == paletteIndex) return false;
    PushUndo();
    m_model.voxels[Index(coordinate)] = paletteIndex;
    CommitMutation();
    return true;
}

bool EditorDocument::Fill(VoxelCoordinate coordinate, std::uint16_t paletteIndex) {
    if (!IsInBounds(coordinate) || paletteIndex > m_model.palette.size()) return false;
    const std::uint16_t original = m_model.voxels[Index(coordinate)];
    if (original == paletteIndex) return false;
    PushUndo();
    std::queue<VoxelCoordinate> pending;
    pending.push(coordinate);
    while (!pending.empty()) {
        const VoxelCoordinate current = pending.front();
        pending.pop();
        if (!IsInBounds(current) || m_model.voxels[Index(current)] != original) continue;
        m_model.voxels[Index(current)] = paletteIndex;
        if (current.x > 0) pending.push({current.x - 1, current.y, current.z});
        if (current.x + 1 < m_model.gridSize[0]) pending.push({current.x + 1, current.y, current.z});
        if (current.y > 0) pending.push({current.x, current.y - 1, current.z});
        if (current.y + 1 < m_model.gridSize[1]) pending.push({current.x, current.y + 1, current.z});
        if (current.z > 0) pending.push({current.x, current.y, current.z - 1});
        if (current.z + 1 < m_model.gridSize[2]) pending.push({current.x, current.y, current.z + 1});
    }
    CommitMutation();
    return true;
}

bool EditorDocument::MoveSelection(const std::vector<VoxelCoordinate>& selection, int offsetX, int offsetY, int offsetZ) {
    std::vector<std::pair<VoxelCoordinate, std::uint16_t>> moved;
    moved.reserve(selection.size());
    for (const VoxelCoordinate coordinate : selection) {
        if (!IsInBounds(coordinate)) continue;
        const int targetX = static_cast<int>(coordinate.x) + offsetX;
        const int targetY = static_cast<int>(coordinate.y) + offsetY;
        const int targetZ = static_cast<int>(coordinate.z) + offsetZ;
        if (targetX < 0 || targetY < 0 || targetZ < 0 || targetX >= m_model.gridSize[0] || targetY >= m_model.gridSize[1] || targetZ >= m_model.gridSize[2]) continue;
        if (const std::uint16_t value = m_model.voxels[Index(coordinate)]; value != 0) moved.push_back({coordinate, value});
    }
    if (moved.empty()) return false;
    PushUndo();
    for (const auto& [coordinate, value] : moved) { static_cast<void>(value); m_model.voxels[Index(coordinate)] = 0; }
    for (const auto& [coordinate, value] : moved) {
        const VoxelCoordinate target{static_cast<std::uint32_t>(static_cast<int>(coordinate.x) + offsetX), static_cast<std::uint32_t>(static_cast<int>(coordinate.y) + offsetY), static_cast<std::uint32_t>(static_cast<int>(coordinate.z) + offsetZ)};
        m_model.voxels[Index(target)] = value;
    }
    CommitMutation();
    return true;
}

bool EditorDocument::Mirror(Axis axis) {
    PushUndo();
    VoxelModel mirrored = m_model;
    for (std::uint32_t z = 0; z < m_model.gridSize[2]; ++z) for (std::uint32_t y = 0; y < m_model.gridSize[1]; ++y) for (std::uint32_t x = 0; x < m_model.gridSize[0]; ++x) {
        std::uint32_t targetX = x, targetY = y, targetZ = z;
        if (axis == Axis::X) targetX = m_model.gridSize[0] - 1U - x;
        if (axis == Axis::Y) targetY = m_model.gridSize[1] - 1U - y;
        if (axis == Axis::Z) targetZ = m_model.gridSize[2] - 1U - z;
        mirrored.voxels[IndexFor(mirrored, targetX, targetY, targetZ)] = m_model.voxels[IndexFor(m_model, x, y, z)];
    }
    m_model = std::move(mirrored);
    CommitMutation();
    return true;
}

bool EditorDocument::Rotate90(Axis axis) {
    const auto oldSize = m_model.gridSize;
    VoxelModel rotated = m_model;
    if (axis == Axis::X) std::swap(rotated.gridSize[1], rotated.gridSize[2]);
    if (axis == Axis::Y) std::swap(rotated.gridSize[0], rotated.gridSize[2]);
    if (axis == Axis::Z) std::swap(rotated.gridSize[0], rotated.gridSize[1]);
    rotated.voxels.assign(rotated.GridVoxelCount(), 0);
    for (std::uint32_t z = 0; z < oldSize[2]; ++z) for (std::uint32_t y = 0; y < oldSize[1]; ++y) for (std::uint32_t x = 0; x < oldSize[0]; ++x) {
        std::uint32_t targetX = x, targetY = y, targetZ = z;
        if (axis == Axis::X) { targetY = oldSize[2] - 1U - z; targetZ = y; }
        if (axis == Axis::Y) { targetX = oldSize[2] - 1U - z; targetZ = x; }
        if (axis == Axis::Z) { targetX = oldSize[1] - 1U - y; targetY = x; }
        rotated.voxels[IndexFor(rotated, targetX, targetY, targetZ)] = m_model.voxels[IndexFor(m_model, x, y, z)];
    }
    PushUndo();
    m_model = std::move(rotated);
    CommitMutation();
    return true;
}

bool EditorDocument::RemovePaletteEntry(std::size_t index) {
    if (index >= m_model.palette.size()) return false;
    PushUndo();
    const std::uint16_t removed = static_cast<std::uint16_t>(index + 1);
    for (std::uint16_t& voxel : m_model.voxels) {
        if (voxel == removed) voxel = 0;
        else if (voxel > removed) --voxel;
    }
    m_model.palette.erase(m_model.palette.begin() + static_cast<std::ptrdiff_t>(index));
    CommitMutation();
    return true;
}

void EditorDocument::AddPaletteEntry(VoxelPaletteEntry entry) { PushUndo(); m_model.palette.push_back(entry); CommitMutation(); }

bool EditorDocument::Undo() {
    if (m_undo.empty()) return false;
    m_redo.push_back(m_model);
    m_model = std::move(m_undo.back());
    m_undo.pop_back();
    m_dirty = true;
    return true;
}

bool EditorDocument::Redo() {
    if (m_redo.empty()) return false;
    m_undo.push_back(m_model);
    m_model = std::move(m_redo.back());
    m_redo.pop_back();
    m_dirty = true;
    return true;
}

bool EditorDocument::Save(const std::filesystem::path& path, std::string& error) {
    try {
        if (!WriteBytesAtomically(path, VmdlCodec::Save(m_model), error)) return false;
        m_dirty = false;
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool EditorDocument::Load(const std::filesystem::path& path, std::string& error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open() || input.tellg() < 0) { error = "could not open " + path.string(); return false; }
    const std::size_t size = static_cast<std::size_t>(input.tellg());
    std::vector<std::byte> bytes(size);
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) { error = "could not read " + path.string(); return false; }
    try { m_model = VmdlCodec::Load(bytes); m_undo.clear(); m_redo.clear(); m_dirty = false; return true; }
    catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool EditorDocument::WriteRecovery(const std::filesystem::path& path, std::string& error) const {
    try { return WriteBytesAtomically(path, VmdlCodec::Save(m_model), error); }
    catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool EditorDocument::ExportObj(const std::filesystem::path& path, std::string& error) const {
    std::error_code filesystemError;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), filesystemError);
    if (filesystemError) { error = "could not create directory for " + path.string(); return false; }
    std::ofstream output(path, std::ios::trunc);
    if (!output.is_open()) { error = "could not open " + path.string(); return false; }
    std::size_t vertex = 1;
    for (std::uint32_t z = 0; z < m_model.gridSize[2]; ++z) for (std::uint32_t y = 0; y < m_model.gridSize[1]; ++y) for (std::uint32_t x = 0; x < m_model.gridSize[0]; ++x) {
        if (!m_model.IsSolidAt(x, y, z)) continue;
        const float scaleX = 1.0F / m_model.gridSize[0], scaleY = 1.0F / m_model.gridSize[1], scaleZ = 1.0F / m_model.gridSize[2];
        const float minX = x * scaleX, minY = y * scaleY, minZ = z * scaleZ;
        output << "v " << minX << ' ' << minY << ' ' << minZ << '\n';
        output << "v " << minX + scaleX << ' ' << minY << ' ' << minZ << '\n';
        output << "v " << minX + scaleX << ' ' << minY + scaleY << ' ' << minZ + scaleZ << '\n';
        output << "v " << minX << ' ' << minY + scaleY << ' ' << minZ + scaleZ << '\n';
        output << "f " << vertex << ' ' << vertex + 1 << ' ' << vertex + 2 << ' ' << vertex + 3 << '\n';
        vertex += 4;
    }
    if (!output) { error = "could not write " + path.string(); return false; }
    return true;
}

EditorProject::EditorProject(std::filesystem::path root) : m_root(std::move(root)) {}

std::vector<std::filesystem::path> EditorProject::ListAssets() const {
    std::vector<std::filesystem::path> assets;
    std::error_code error;
    if (!std::filesystem::is_directory(m_root, error)) return assets;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(m_root, error)) {
        if (error) return assets;
        if (entry.is_regular_file()) assets.push_back(std::filesystem::relative(entry.path(), m_root));
    }
    std::sort(assets.begin(), assets.end());
    return assets;
}

bool EditorProject::CreateLayout(std::string& error) const {
    std::error_code filesystemError;
    for (const char* directory : {"models", "textures", "data", "audio", "shaders"}) std::filesystem::create_directories(m_root / directory, filesystemError);
    if (filesystemError) { error = "could not create project layout at " + m_root.string(); return false; }
    return true;
}

bool EditorProject::BuildPack(const std::filesystem::path& output, AssetBundleReport& report, std::string& error) const { return AssetBundler::Bundle(m_root, output, report, error); }

bool EditorProject::CreateBlockTexture(const std::string& textureId, const ImageData& image, std::string& error) const {
    if (textureId.empty() || textureId.find("..") != std::string::npos || image.width != 16 || image.height != 16 || image.channels != 4) { error = "texture must be a 16x16 RGBA image with a safe id"; return false; }
    const std::filesystem::path path = m_root / "textures" / (textureId + ".png");
    if (!TextureLoader::WritePngToFile(path, image)) { error = "could not write " + path.string(); return false; }
    return true;
}

bool EditorProject::UpsertModelBlock(const std::string& id, const std::string& displayName, const std::string& modelId,
                                     const std::string& textureId, float hardness, std::string& error) const {
    if (id.empty() || modelId.empty() || id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos) { error = "block id must use lowercase letters, digits, _ or -"; return false; }
    const std::filesystem::path modelPath = m_root / modelId;
    if (!std::filesystem::exists(modelPath)) { error = "model does not exist: " + modelPath.string(); return false; }
    const std::filesystem::path texturePath = m_root / "textures" / (textureId + ".png");
    if (!textureId.empty() && !TextureLoader::LoadFromFile(texturePath)) { error = "texture is not a valid PNG: " + texturePath.string(); return false; }
    const std::filesystem::path path = m_root / "data" / "blocks.json";
    nlohmann::json document = {{"blocks", nlohmann::json::array()}};
    if (std::ifstream input(path); input) { try { input >> document; } catch (const nlohmann::json::exception& exception) { error = exception.what(); return false; } }
    auto& blocks = document["blocks"];
    int nextId = 0;
    for (const auto& block : blocks) nextId = std::max(nextId, block.value("numeric_id", -1) + 1);
    nlohmann::json definition = {{"id", id}, {"display_name", displayName}, {"numeric_id", nextId}, {"solid", true}, {"opaque", true}, {"liquid", false}, {"hardness", hardness}, {"light_emission", 0}, {"textures", textureId.empty() ? nlohmann::json::object() : nlohmann::json{{"all", textureId}}}, {"render_type", "model"}, {"model_id", modelId}, {"sounds", nlohmann::json::object()}, {"drops", nlohmann::json::array()}};
    bool replaced = false;
    for (auto& block : blocks) if (block.value("id", "") == id) { definition["numeric_id"] = block.value("numeric_id", nextId); block = definition; replaced = true; break; }
    if (!replaced) blocks.push_back(std::move(definition));
    try { voxels::BlockRegistry registry; registry.LoadFromJsonString(document.dump()); } catch (const std::exception& exception) { error = exception.what(); return false; }
    std::ofstream output(path, std::ios::trunc);
    if (!output) { error = "could not open " + path.string(); return false; }
    output << document.dump(2) << '\n';
    return static_cast<bool>(output);
}

TextureDocument::TextureDocument(int width, int height) {
    m_image.width = width; m_image.height = height; m_image.channels = 4;
    m_image.pixels.assign(static_cast<std::size_t>(width * height * 4), 255);
}

bool TextureDocument::SetPixel(int x, int y, std::array<std::uint8_t, 4> color) noexcept {
    if (x < 0 || y < 0 || x >= m_image.width || y >= m_image.height) return false;
    const std::size_t offset = static_cast<std::size_t>((y * m_image.width + x) * 4);
    std::copy(color.begin(), color.end(), m_image.pixels.begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
}

bool TextureDocument::Save(const std::filesystem::path& path) const { return TextureLoader::WritePngToFile(path, m_image); }

} // namespace voxels::editor