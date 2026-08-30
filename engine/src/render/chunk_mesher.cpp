/**
 * @file chunk_mesher.cpp
 * @brief Greedy, neighbour-aware chunk mesher: AO, sky/block light, and transparency split.
 */

#include "voxels/render/chunk_mesher.hpp"

#include <algorithm>
#include <utility>

namespace voxels::graphics {

namespace {

using voxels::BlockDefinition;
using voxels::BlockId;
using voxels::BlockRegistry;
using voxels::BlockType;
using voxels::Chunk;
using voxels::Face;

constexpr int kAxisU[3] = {1, 2, 0};
constexpr int kAxisV[3] = {2, 0, 1};

Face FaceForAxis(int axis, bool positiveDirection) {
    switch (axis) {
        case 0:
            return positiveDirection ? Face::PosX : Face::NegX;
        case 1:
            return positiveDirection ? Face::PosY : Face::NegY;
        default:
            return positiveDirection ? Face::PosZ : Face::NegZ;
    }
}

/// Per-unit-cell exposed-face description used as the greedy-merge mask value. Two adjacent
/// cells may only merge into one quad when every field here matches exactly.
struct FaceCell {
    bool present = false;
    BlockId blockId = 0;
    std::uint16_t atlasLayer = 0;
    bool transparent = false;
    std::uint8_t skyLight = 0;
    std::uint8_t blockLight = 0;
    std::uint8_t tint = 0;
    std::array<std::uint8_t, 4> ao{3, 3, 3, 3};
    bool waterSurface = false;
    Face face = Face::PosY;

    [[nodiscard]] bool MergeEquals(const FaceCell& other) const noexcept {
        return present && other.present && blockId == other.blockId && atlasLayer == other.atlasLayer &&
               transparent == other.transparent && skyLight == other.skyLight && blockLight == other.blockLight &&
               tint == other.tint && ao == other.ao && waterSurface == other.waterSurface && face == other.face;
    }
};

/// Returns true if `def` should draw a face toward a neighbour described by `neighborDef`.
bool ShouldEmitFace(BlockId id, const BlockDefinition* def, BlockId neighborId, const BlockDefinition* neighborDef) {
    if (def == nullptr) {
        return false;
    }
    const bool isAirLike = !def->isSolid && !def->isLiquid;
    if (isAirLike) {
        return false;
    }
    if (neighborDef == nullptr) {
        return true;
    }
    const bool neighborAirLike = !neighborDef->isSolid && !neighborDef->isLiquid;
    if (neighborAirLike) {
        return true;
    }
    if (def->isOpaque) {
        return !neighborDef->isOpaque;
    }
    // This block is transparent or a liquid.
    if (neighborDef->isOpaque) {
        return false; // fully hidden behind an opaque neighbour
    }
    if (neighborId == id) {
        return false; // identical transparent/liquid faces cull against each other
    }
    return true;
}

} // namespace

ChunkMeshData BuildChunkMesh(const Chunk& chunk, const ChunkNeighborhood& neighborhood, const BlockRegistry& registry,
                              const TextureAtlas& atlas) {
    ChunkMeshData result;

    const int sx = static_cast<int>(chunk.GetWidth());
    const int sy = static_cast<int>(chunk.GetHeight());
    const int sz = static_cast<int>(chunk.GetDepth());
    const int dims[3] = {sx, sy, sz};

    const auto& neighbors = neighborhood.neighbors;

    // Resolves a coordinate that may be exactly one axis out of [0, dims[axis]) into the chunk
    // (center or face-neighbour) that owns it. Returns {id, known}; known=false means the owning
    // neighbour chunk is not resident.
    const auto SampleFace = [&](int x, int y, int z) -> std::pair<BlockId, bool> {
        int ox = x, oy = y, oz = z;
        const Chunk* target = &chunk;
        if (x < 0) {
            target = neighbors[static_cast<std::size_t>(Face::NegX)];
            ox = x + sx;
        } else if (x >= sx) {
            target = neighbors[static_cast<std::size_t>(Face::PosX)];
            ox = x - sx;
        } else if (y < 0) {
            target = neighbors[static_cast<std::size_t>(Face::NegY)];
            oy = y + sy;
        } else if (y >= sy) {
            target = neighbors[static_cast<std::size_t>(Face::PosY)];
            oy = y - sy;
        } else if (z < 0) {
            target = neighbors[static_cast<std::size_t>(Face::NegZ)];
            oz = z + sz;
        } else if (z >= sz) {
            target = neighbors[static_cast<std::size_t>(Face::PosZ)];
            oz = z - sz;
        }
        if (target == nullptr) {
            return {0, false};
        }
        return {target->GetBlock(ox, oy, oz), true};
    };

    const auto SampleLight = [&](int x, int y, int z) -> std::pair<std::uint8_t, std::uint8_t> {
        int ox = x, oy = y, oz = z;
        const Chunk* target = &chunk;
        if (x < 0) {
            target = neighbors[static_cast<std::size_t>(Face::NegX)];
            ox = x + sx;
        } else if (x >= sx) {
            target = neighbors[static_cast<std::size_t>(Face::PosX)];
            ox = x - sx;
        } else if (y < 0) {
            target = neighbors[static_cast<std::size_t>(Face::NegY)];
            oy = y + sy;
        } else if (y >= sy) {
            target = neighbors[static_cast<std::size_t>(Face::PosY)];
            oy = y - sy;
        } else if (z < 0) {
            target = neighbors[static_cast<std::size_t>(Face::NegZ)];
            oz = z + sz;
        } else if (z >= sz) {
            target = neighbors[static_cast<std::size_t>(Face::PosZ)];
            oz = z - sz;
        }
        if (target == nullptr) {
            return {15, 0};
        }
        return {target->GetSkyLight(ox, oy, oz), target->GetBlockLight(ox, oy, oz)};
    };

    // AO occlusion sampling. Two chunks diagonally out of range (edge/corner neighbours we do
    // not have a pointer to) are conservatively treated as open/non-solid.
    const auto IsSolidForAO = [&](int x, int y, int z) -> bool {
        int outOfRangeAxes = 0;
        if (x < 0 || x >= sx) ++outOfRangeAxes;
        if (y < 0 || y >= sy) ++outOfRangeAxes;
        if (z < 0 || z >= sz) ++outOfRangeAxes;
        if (outOfRangeAxes >= 2) {
            return false;
        }
        const auto [id, known] = SampleFace(x, y, z);
        if (!known) {
            return false;
        }
        const BlockDefinition* def = registry.GetDefinition(id);
        return def != nullptr && def->isSolid && !def->isTransparent && !def->isLiquid;
    };

    bool anyProvisional = false;

    // Two staging buffers so opaque and transparent geometry can be concatenated afterwards
    // with a single, contiguous index range each.
    std::vector<ChunkVertex> opaqueVerts;
    std::vector<std::uint32_t> opaqueIdx;
    std::vector<ChunkVertex> transparentVerts;
    std::vector<std::uint32_t> transparentIdx;

    for (int axis = 0; axis < 3; ++axis) {
        const int u = kAxisU[axis];
        const int v = kAxisV[axis];
        int pos[3] = {0, 0, 0};
        int step[3] = {0, 0, 0};
        step[axis] = 1;

        std::vector<FaceCell> mask(static_cast<std::size_t>(dims[u]) * static_cast<std::size_t>(dims[v]));
        std::vector<std::uint8_t> maskSign(mask.size(), 0); // 1 = positive-direction face

        for (pos[axis] = -1; pos[axis] < dims[axis];) {
            std::size_t idx = 0;
            for (pos[v] = 0; pos[v] < dims[v]; ++pos[v]) {
                for (pos[u] = 0; pos[u] < dims[u]; ++pos[u], ++idx) {
                    mask[idx] = FaceCell{};

                    const int ax = pos[0], ay = pos[1], az = pos[2];
                    const int bx = pos[0] + step[0], by = pos[1] + step[1], bz = pos[2] + step[2];

                    const auto [aId, aKnown] = SampleFace(ax, ay, az);
                    const auto [bId, bKnown] = SampleFace(bx, by, bz);

                    if (!aKnown || !bKnown) {
                        // Only geometry that actually borders the unknown side is ambiguous; an
                        // empty region next to an unresident neighbour never needs a face either
                        // way, so it must not spuriously mark the whole chunk provisional.
                        const BlockId knownId = aKnown ? aId : bId;
                        const BlockDefinition* knownDef = registry.GetDefinition(knownId);
                        const bool knownIsAirLike = knownDef == nullptr || (!knownDef->isSolid && !knownDef->isLiquid);
                        if (!knownIsAirLike) {
                            anyProvisional = true;
                        }
                        continue;
                    }

                    const BlockDefinition* aDef = registry.GetDefinition(aId);
                    const BlockDefinition* bDef = registry.GetDefinition(bId);

                    const bool aEmits = ShouldEmitFace(aId, aDef, bId, bDef);
                    const bool bEmits = !aEmits && ShouldEmitFace(bId, bDef, aId, aDef);
                    if (!aEmits && !bEmits) {
                        continue;
                    }

                    const bool sign = aEmits; // true = normal points +axis (owned by 'a')
                    const BlockId ownerId = aEmits ? aId : bId;
                    const BlockDefinition* ownerDef = aEmits ? aDef : bDef;
                    const auto [neighborId, neighborDef] = aEmits ? std::pair{bId, bDef} : std::pair{aId, aDef};

                    const Face face = FaceForAxis(axis, sign);
                    const auto [skyLight, blockLight] = SampleLight(sign ? bx : ax, sign ? by : ay, sign ? bz : az);

                    FaceCell cell;
                    cell.present = true;
                    cell.blockId = ownerId;
                    cell.atlasLayer = static_cast<std::uint16_t>(atlas.LayerFor(ownerDef->GetFaceTexture(face)));
                    cell.transparent = !ownerDef->isOpaque;
                    cell.skyLight = skyLight;
                    cell.blockLight = blockLight;
                    cell.tint = (ownerDef->tintColor[0] != 1.0f || ownerDef->tintColor[1] != 1.0f ||
                                 ownerDef->tintColor[2] != 1.0f)
                                    ? 1
                                    : 0;
                    cell.waterSurface = ownerDef->isLiquid && face == Face::PosY &&
                                         !(neighborDef != nullptr && neighborDef->isLiquid && neighborId == ownerId);
                    cell.face = face;

                    // Ambient occlusion: sample the boundary layer (the empty side of the face).
                    const int aoLayer = sign ? (axis == 0 ? bx : (axis == 1 ? by : bz))
                                              : (axis == 0 ? ax : (axis == 1 ? ay : az));
                    static constexpr int kCornerDx[4] = {-1, 1, 1, -1};
                    static constexpr int kCornerDy[4] = {-1, -1, 1, 1};
                    for (int c = 0; c < 4; ++c) {
                        const int dx = kCornerDx[c];
                        const int dy = kCornerDy[c];
                        const int cu = pos[u];
                        const int cv = pos[v];
                        const auto Sample3D = [&](int du, int dv) {
                            int coord[3];
                            coord[axis] = aoLayer;
                            coord[u] = cu + du;
                            coord[v] = cv + dv;
                            return IsSolidForAO(coord[0], coord[1], coord[2]);
                        };
                        const bool side1 = Sample3D(dx, 0);
                        const bool side2 = Sample3D(0, dy);
                        const bool corner = Sample3D(dx, dy);
                        cell.ao[static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                            (side1 && side2) ? 0 : (3 - (static_cast<int>(side1) + static_cast<int>(side2) + static_cast<int>(corner))));
                    }

                    mask[idx] = cell;
                    maskSign[idx] = sign ? 1 : 0;
                }
            }

            ++pos[axis];

            // Greedy-merge the mask into the fewest possible axis-aligned rectangles.
            idx = 0;
            for (int j = 0; j < dims[v]; ++j) {
                for (int i = 0; i < dims[u];) {
                    const FaceCell& cell = mask[idx];
                    if (!cell.present) {
                        ++i;
                        ++idx;
                        continue;
                    }
                    const std::uint8_t sign = maskSign[idx];

                    int width = 1;
                    while (i + width < dims[u] && mask[idx + static_cast<std::size_t>(width)].MergeEquals(cell) &&
                           maskSign[idx + static_cast<std::size_t>(width)] == sign) {
                        ++width;
                    }

                    int height = 1;
                    bool blocked = false;
                    while (j + height < dims[v] && !blocked) {
                        for (int k = 0; k < width; ++k) {
                            const std::size_t testIdx = idx + static_cast<std::size_t>(k) +
                                                         static_cast<std::size_t>(height) * static_cast<std::size_t>(dims[u]);
                            if (!mask[testIdx].MergeEquals(cell) || maskSign[testIdx] != sign) {
                                blocked = true;
                                break;
                            }
                        }
                        if (!blocked) {
                            ++height;
                        }
                    }

                    // Emit the merged quad.
                    int quadPos[3] = {pos[0], pos[1], pos[2]};
                    quadPos[u] = i;
                    quadPos[v] = j;

                    const int corners[4][2] = {{i, j}, {i + width, j}, {i + width, j + height}, {i, j + height}};
                    std::array<std::array<float, 3>, 4> positions{};
                    for (int c = 0; c < 4; ++c) {
                        int coord[3];
                        coord[axis] = quadPos[axis];
                        coord[u] = corners[c][0];
                        coord[v] = corners[c][1];
                        positions[static_cast<std::size_t>(c)] = {static_cast<float>(coord[0]), static_cast<float>(coord[1]),
                                                                    static_cast<float>(coord[2])};
                    }
                    if (cell.waterSurface) {
                        constexpr float kWaterSurfaceDrop = 0.1f;
                        for (auto& corner : positions) {
                            corner[1] -= kWaterSurfaceDrop;
                        }
                    }

                    const std::uint8_t widthUv = static_cast<std::uint8_t>(width);
                    const std::uint8_t heightUv = static_cast<std::uint8_t>(height);
                    std::array<std::array<std::uint8_t, 2>, 4> uvs = {
                        std::array<std::uint8_t, 2>{0, 0},
                        std::array<std::uint8_t, 2>{widthUv, 0},
                        std::array<std::uint8_t, 2>{widthUv, heightUv},
                        std::array<std::uint8_t, 2>{0, heightUv}};
                    if (cell.face == Face::PosX || cell.face == Face::NegX) {
                        // X-facing masks are built as Y-by-Z rectangles. Swap their UV axes so
                        // texture V follows world height and vertical textures remain upright.
                        uvs = {{{0, 0}, {0, widthUv}, {heightUv, widthUv}, {heightUv, 0}}};
                    } else if (cell.face == Face::PosY || cell.face == Face::NegY) {
                        // Horizontal masks are built as Z-by-X rectangles; keep U east-west.
                        uvs = {{{0, 0}, {0, widthUv}, {heightUv, widthUv}, {heightUv, 0}}};
                    }

                    std::vector<ChunkVertex>& outVerts = cell.transparent ? transparentVerts : opaqueVerts;
                    std::vector<std::uint32_t>& outIdx = cell.transparent ? transparentIdx : opaqueIdx;

                    const std::uint32_t baseIndex = static_cast<std::uint32_t>(outVerts.size());
                    for (int c = 0; c < 4; ++c) {
                        ChunkVertex vertex;
                        vertex.x = static_cast<std::uint16_t>(positions[static_cast<std::size_t>(c)][0] * kChunkVertexPositionScale);
                        vertex.y = static_cast<std::uint16_t>(positions[static_cast<std::size_t>(c)][1] * kChunkVertexPositionScale);
                        vertex.z = static_cast<std::uint16_t>(positions[static_cast<std::size_t>(c)][2] * kChunkVertexPositionScale);
                        vertex.faceIndex = static_cast<std::uint8_t>(cell.face);
                        vertex.ao = cell.ao[static_cast<std::size_t>(c)];
                        vertex.skyLight = cell.skyLight;
                        vertex.blockLight = cell.blockLight;
                        vertex.atlasLayer = cell.atlasLayer;
                        vertex.u = uvs[static_cast<std::size_t>(c)][0];
                        vertex.v = uvs[static_cast<std::size_t>(c)][1];
                        vertex.tint = cell.tint;
                        outVerts.push_back(vertex);
                    }

                    // Anti-flip triangulation: pick the diagonal that best matches the AO gradient.
                    const bool flip = (cell.ao[0] + cell.ao[2]) > (cell.ao[1] + cell.ao[3]);
                    std::array<std::uint32_t, 3> tri1 = flip ? std::array<std::uint32_t, 3>{1, 2, 3}
                                                              : std::array<std::uint32_t, 3>{0, 1, 2};
                    std::array<std::uint32_t, 3> tri2 = flip ? std::array<std::uint32_t, 3>{1, 3, 0}
                                                              : std::array<std::uint32_t, 3>{0, 2, 3};
                    if (sign == 0) {
                        std::swap(tri1[1], tri1[2]);
                        std::swap(tri2[1], tri2[2]);
                    }
                    for (auto index : tri1) outIdx.push_back(baseIndex + index);
                    for (auto index : tri2) outIdx.push_back(baseIndex + index);

                    for (int hh = 0; hh < height; ++hh) {
                        for (int ww = 0; ww < width; ++ww) {
                            const std::size_t clearIdx =
                                idx + static_cast<std::size_t>(ww) + static_cast<std::size_t>(hh) * static_cast<std::size_t>(dims[u]);
                            mask[clearIdx] = FaceCell{};
                        }
                    }

                    i += width;
                    idx += static_cast<std::size_t>(width);
                }
            }
        }
    }

    result.provisional = anyProvisional;
    result.vertices = std::move(opaqueVerts);
    result.indices = std::move(opaqueIdx);
    result.opaqueIndexCount = static_cast<std::uint32_t>(result.indices.size());

    const std::uint32_t vertexOffset = static_cast<std::uint32_t>(result.vertices.size());
    result.vertices.insert(result.vertices.end(), transparentVerts.begin(), transparentVerts.end());
    for (auto index : transparentIdx) {
        result.indices.push_back(index + vertexOffset);
    }
    result.transparentIndexCount = static_cast<std::uint32_t>(transparentIdx.size());

    return result;
}

} // namespace voxels::graphics
