/**
 * @file test_block_catalogue.cpp
 * @brief Automated unit and integration tests for Work Item 04: block definitions,
 *        texture loading, procedural forging, and texture atlas.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <set>
#include <vector>

#include "voxels/assets/texture_loader.hpp"
#include "voxels/render/texture_atlas.hpp"
#include "voxels/render/texture_forge.hpp"
#include "voxels/world/block.hpp"

TEST_CASE("BlockRegistry.LoadsAllLaunchBlocksWithExpectedFlags", "[world][block]") {
    const voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();

    REQUIRE(registry.Count() == 20);

    const std::vector<std::string> expectedLaunchBlocks = {
        "air", "stone", "dirt", "grass", "sand", "gravel", "water",
        "coal_ore", "iron_ore", "wood_log", "leaves", "planks", "glass", "bedrock", "stairs", "slab",
        "coal", "stick", "garden_mix", "stone_axe"
    };

    std::set<voxels::BlockId> numericIds;
    for (const auto& name : expectedLaunchBlocks) {
        REQUIRE(registry.IsRegistered(name));
        const auto* def = registry.GetDefinition(name);
        REQUIRE(def != nullptr);
        REQUIRE(def->name == name);
        numericIds.insert(def->id);
    }
    REQUIRE(numericIds.size() == 20);
    REQUIRE(registry.ValidateDenseNumericIds());

    // Spot-check physical flags and metadata
    const auto* air = registry.GetDefinition("air");
    REQUIRE(air != nullptr);
    REQUIRE(air->id == 0);
    REQUIRE_FALSE(air->isSolid);
    REQUIRE_FALSE(air->isOpaque);
    REQUIRE_FALSE(air->isLiquid);

    const auto* stone = registry.GetDefinition("stone");
    REQUIRE(stone != nullptr);
    REQUIRE(stone->id == 1);
    REQUIRE(stone->isSolid);
    REQUIRE(stone->isOpaque);
    REQUIRE(stone->hardness == Catch::Approx(1.5f));
    REQUIRE(stone->GetFaceTexture(voxels::Face::PosY) == "blocks/stone");

    const auto* grass = registry.GetDefinition("grass");
    REQUIRE(grass != nullptr);
    REQUIRE(grass->id == 3);
    REQUIRE(grass->isSolid);
    REQUIRE(grass->GetFaceTexture(voxels::Face::PosY) == "blocks/grass_top");
    REQUIRE(grass->GetFaceTexture(voxels::Face::NegY) == "blocks/dirt");
    REQUIRE(grass->GetFaceTexture(voxels::Face::PosX) == "blocks/grass_side");
    REQUIRE(grass->tintColor[0] == Catch::Approx(0.45f));

    const auto* water = registry.GetDefinition("water");
    REQUIRE(water != nullptr);
    REQUIRE(water->id == 6);
    REQUIRE_FALSE(water->isSolid);
    REQUIRE(water->isLiquid);
    REQUIRE(water->renderType == "liquid");

    const auto* leaves = registry.GetDefinition("leaves");
    REQUIRE(leaves != nullptr);
    REQUIRE(leaves->id == 10);
    REQUIRE(leaves->isSolid);
    REQUIRE_FALSE(leaves->isOpaque);
    REQUIRE(leaves->isTransparent);
    REQUIRE(leaves->tintColor[0] == Catch::Approx(0.38f));

    const auto* glass = registry.GetDefinition("glass");
    REQUIRE(glass != nullptr);
    REQUIRE(glass->id == 12);
    REQUIRE(glass->isSolid);
    REQUIRE_FALSE(glass->isOpaque);
    REQUIRE(glass->isTransparent);

    const auto* bedrock = registry.GetDefinition("bedrock");
    REQUIRE(bedrock != nullptr);
    REQUIRE(bedrock->id == 13);
    REQUIRE(bedrock->isSolid);
    REQUIRE(bedrock->hardness < 0.0f);

    // Inventory-only items (coal, stick, garden mix, stone axe) are real registered items,
    // not aliases of a placeable world block, and are rejected by placement.
    for (const std::string& itemName : {"coal", "stick", "garden_mix", "stone_axe"}) {
        const auto* item = registry.GetDefinition(itemName);
        REQUIRE(item != nullptr);
        REQUIRE_FALSE(item->isPlaceable);
    }
    REQUIRE(registry.GetDefinition("coal")->id != registry.GetDefinition("coal_ore")->id);
}

TEST_CASE("BlockRegistry.RejectsDuplicateOrMalformedDefinitions", "[world][block]") {
    voxels::BlockRegistry registry;

    SECTION("Duplicate string id is rejected with descriptive error") {
        const char* duplicateStringIdJson = R"({
            "blocks": [
                { "id": "stone", "numeric_id": 0 },
                { "id": "stone", "numeric_id": 1 }
            ]
        })";
        bool threw = false;
        try {
            registry.LoadFromJsonString(duplicateStringIdJson, "test_dup_str.json");
        } catch (const std::exception& e) {
            threw = true;
            REQUIRE(std::string(e.what()).find("duplicate string id 'stone'") != std::string::npos);
        }
        REQUIRE(threw);
    }

    SECTION("Duplicate numeric id is rejected with descriptive error") {
        const char* duplicateNumericIdJson = R"({
            "blocks": [
                { "id": "stone", "numeric_id": 0 },
                { "id": "dirt", "numeric_id": 0 }
            ]
        })";
        bool threw = false;
        try {
            registry.LoadFromJsonString(duplicateNumericIdJson, "test_dup_num.json");
        } catch (const std::exception& e) {
            threw = true;
            REQUIRE(std::string(e.what()).find("duplicate numeric_id 0") != std::string::npos);
        }
        REQUIRE(threw);
    }

    SECTION("Non-dense numeric sequence is rejected") {
        const char* nonDenseJson = R"({
            "blocks": [
                { "id": "air", "numeric_id": 0 },
                { "id": "stone", "numeric_id": 2 }
            ]
        })";
        bool threw = false;
        try {
            registry.LoadFromJsonString(nonDenseJson, "test_non_dense.json");
        } catch (const std::exception& e) {
            threw = true;
            REQUIRE(std::string(e.what()).find("density validation failed") != std::string::npos);
        }
        REQUIRE(threw);
    }

    SECTION("Malformed JSON syntax is rejected") {
        const char* badJson = "{ invalid json content !!!";
        bool threw = false;
        try {
            registry.LoadFromJsonString(badJson, "bad.json");
        } catch (const std::exception& e) {
            threw = true;
            REQUIRE(std::string(e.what()).find("JSON parse error") != std::string::npos);
        }
        REQUIRE(threw);
    }

    SECTION("Missing blocks array is rejected") {
        const char* noBlocksJson = R"({ "other_data": 123 })";
        bool threw = false;
        try {
            registry.LoadFromJsonString(noBlocksJson, "no_blocks.json");
        } catch (const std::exception& e) {
            threw = true;
            REQUIRE(std::string(e.what()).find("must contain 'blocks' array") != std::string::npos);
        }
        REQUIRE(threw);
    }
}

TEST_CASE("BlockRegistry.PerFaceTextureResolutionFallsBackToAll", "[world][block]") {
    voxels::BlockDefinition defAll;
    defAll.id = 1;
    defAll.name = "test_all";
    defAll.textures.all = "blocks/test_all";

    REQUIRE(defAll.GetFaceTexture(voxels::Face::PosY) == "blocks/test_all");
    REQUIRE(defAll.GetFaceTexture(voxels::Face::NegY) == "blocks/test_all");
    REQUIRE(defAll.GetFaceTexture(voxels::Face::PosX) == "blocks/test_all");
    REQUIRE(defAll.GetFaceTexture(voxels::Face::NegX) == "blocks/test_all");
    REQUIRE(defAll.GetFaceTexture(voxels::Face::PosZ) == "blocks/test_all");
    REQUIRE(defAll.GetFaceTexture(voxels::Face::NegZ) == "blocks/test_all");

    voxels::BlockDefinition defPerFace;
    defPerFace.id = 2;
    defPerFace.name = "test_per_face";
    defPerFace.textures.all = "blocks/fallback";
    defPerFace.textures.top = "blocks/custom_top";
    defPerFace.textures.bottom = "blocks/custom_bottom";
    defPerFace.textures.side = "blocks/custom_side";
    defPerFace.textures.north = "blocks/custom_north";

    REQUIRE(defPerFace.GetFaceTexture(voxels::Face::PosY) == "blocks/custom_top");
    REQUIRE(defPerFace.GetFaceTexture(voxels::Face::NegY) == "blocks/custom_bottom");
    REQUIRE(defPerFace.GetFaceTexture(voxels::Face::NegZ) == "blocks/custom_north");
    REQUIRE(defPerFace.GetFaceTexture(voxels::Face::PosZ) == "blocks/custom_side");
    REQUIRE(defPerFace.GetFaceTexture(voxels::Face::PosX) == "blocks/custom_side");
    REQUIRE(defPerFace.GetFaceTexture(voxels::Face::NegX) == "blocks/custom_side");
}

TEST_CASE("TextureAtlas.AssignsUniqueLayerPerTextureAndIsStableAcrossRuns", "[render][atlas]") {
    voxels::TextureAtlas atlas1(16, 16);
    voxels::TextureAtlas atlas2(16, 16);

    const auto names = voxels::TextureForge::GetLaunchTextureNames();

    // Register in forward order on atlas1
    for (const auto& name : names) {
        atlas1.RegisterTexture("blocks/" + name, voxels::TextureForge::GenerateTexture(name, 100U));
    }

    // Register in reverse order on atlas2
    for (auto it = names.rbegin(); it != names.rend(); ++it) {
        atlas2.RegisterTexture("blocks/" + *it, voxels::TextureForge::GenerateTexture(*it, 100U));
    }

    REQUIRE(atlas1.BuildGLTexture());
    REQUIRE(atlas2.BuildGLTexture());

    REQUIRE(atlas1.GetLayerCount() == atlas2.GetLayerCount());
    REQUIRE(atlas1.GetLayerCount() == static_cast<int>(names.size()) + 1); // +1 for __missing__

    // Layer 0 is always __missing__
    REQUIRE(atlas1.LayerFor(voxels::TextureAtlas::kMissingTextureKey) == 0);
    REQUIRE(atlas2.LayerFor(voxels::TextureAtlas::kMissingTextureKey) == 0);

    // Layer assignment must be deterministic regardless of insertion order
    std::set<int> layers1;
    std::set<int> layers2;
    for (const auto& name : names) {
        const int l1 = atlas1.LayerFor("blocks/" + name);
        const int l2 = atlas2.LayerFor("blocks/" + name);

        REQUIRE(l1 > 0);
        REQUIRE(l1 == l2);

        layers1.insert(l1);
        layers2.insert(l2);
    }
    REQUIRE(layers1.size() == names.size());
}

TEST_CASE("TextureAtlas.MissingTextureResolvesToCheckerPlaceholder", "[render][atlas]") {
    voxels::TextureAtlas atlas(16, 16);
    atlas.RegisterTexture("blocks/stone", voxels::TextureForge::GenerateTexture("stone"));
    REQUIRE(atlas.BuildGLTexture());

    const int missingLayer = atlas.LayerFor("non_existent_texture_name_xyz");
    REQUIRE(missingLayer == 0);

    const auto* missingImg = atlas.GetLayerImage(0);
    REQUIRE(missingImg != nullptr);
    REQUIRE(missingImg->width == 16);
    REQUIRE(missingImg->height == 16);

    // Verify checkerboard pattern has magenta (#FF00FF) and black
    bool hasMagenta = false;
    bool hasBlack = false;
    for (std::size_t i = 0; i < missingImg->pixels.size(); i += 4) {
        const auto r = missingImg->pixels[i + 0];
        const auto g = missingImg->pixels[i + 1];
        const auto b = missingImg->pixels[i + 2];
        if (r == 255 && g == 0 && b == 255) hasMagenta = true;
        if (r == 0 && g == 0 && b == 0) hasBlack = true;
    }
    REQUIRE(hasMagenta);
    REQUIRE(hasBlack);
}

TEST_CASE("TextureAtlas.DumpAtlasToPng", "[render][atlas]") {
    voxels::TextureAtlas atlas(16, 16);
    const auto registry = voxels::CreateDefaultBlockRegistry();
    atlas.PopulateFromBlockRegistry(registry, "app/assets/textures");
    REQUIRE(atlas.BuildGLTexture());

    const std::filesystem::path dumpPath = "build/test_atlas_dump.png";
    REQUIRE(atlas.DumpAtlasToPng(dumpPath));
    REQUIRE(std::filesystem::exists(dumpPath));

    const auto loaded = voxels::TextureLoader::LoadFromFile(dumpPath, false, false);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->width > 0);
    REQUIRE(loaded->height > 0);
    REQUIRE(loaded->channels == 4);

    std::error_code ec;
    std::filesystem::remove(dumpPath, ec);
}

TEST_CASE("TextureForge.GeneratesDeterministicTexturesForSeed", "[assets][forge]") {
    const auto textureNames = voxels::TextureForge::GetLaunchTextureNames();
    REQUIRE_FALSE(textureNames.empty());

    for (const auto& name : textureNames) {
        const voxels::ImageData img1 = voxels::TextureForge::GenerateTexture(name, 42U);
        const voxels::ImageData img2 = voxels::TextureForge::GenerateTexture(name, 42U);

        REQUIRE(img1.width == 16);
        REQUIRE(img1.height == 16);
        REQUIRE(img1.channels == 4);
        REQUIRE(img1.pixels.size() == 16 * 16 * 4);
        REQUIRE(img1.pixels == img2.pixels);
    }
}

TEST_CASE("TextureForge.ForgesAllLaunchTexturesToDisk", "[assets][forge]") {
#ifdef VOXELS_SOURCE_DIR
    const std::filesystem::path targetDir = std::filesystem::path(VOXELS_SOURCE_DIR) / "app" / "assets" / "textures" / "blocks";
#else
    const std::filesystem::path targetDir = "app/assets/textures/blocks";
#endif
    const std::size_t count = voxels::TextureForge::ForgeLaunchTextures(targetDir, true, 1337U);
    REQUIRE(count == 15);

    for (const auto& name : voxels::TextureForge::GetLaunchTextureNames()) {
        const std::filesystem::path filePath = targetDir / (name + ".png");
        REQUIRE(std::filesystem::exists(filePath));
        const auto loaded = voxels::TextureLoader::LoadFromFile(filePath);
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->width == 16);
        REQUIRE(loaded->height == 16);
        REQUIRE(loaded->channels == 4);
    }
}

TEST_CASE("TextureForge.ForgesInteractionAssetsToDisk", "[assets][forge][interaction]") {
    const std::filesystem::path targetDir = "build/test_interaction_assets";
    REQUIRE(voxels::TextureForge::ForgeInteractionAssets(targetDir, true) == 13);

    const auto crosshair = voxels::TextureLoader::LoadFromFile(targetDir / "ui/crosshair.png");
    REQUIRE(crosshair.has_value());
    REQUIRE(crosshair->width == 16);
    REQUIRE(crosshair->height == 16);

    const auto hotbar = voxels::TextureLoader::LoadFromFile(targetDir / "ui/hotbar.png", false, false);
    REQUIRE(hotbar.has_value());
    REQUIRE(hotbar->width == 182);
    REQUIRE(hotbar->height == 22);

    const auto crack = voxels::TextureLoader::LoadFromFile(targetDir / "textures/misc/crack_9.png");
    REQUIRE(crack.has_value());
    REQUIRE(crack->width == 16);
    REQUIRE(crack->height == 16);
    REQUIRE(crack->pixels != voxels::TextureForge::GenerateCrackTexture(0).pixels);

    std::error_code ec;
    std::filesystem::remove_all(targetDir, ec);
}

TEST_CASE("TextureLoader.DecodesGeneratedPngRoundTrip", "[assets][loader]") {
    const voxels::ImageData original = voxels::TextureForge::GenerateTexture("blocks/stone", 1234U);
    REQUIRE(original.width == 16);
    REQUIRE(original.height == 16);

    const std::vector<std::uint8_t> pngBytes = voxels::TextureLoader::EncodePngToMemory(
        original.width, original.height, original.channels, original.pixels.data());
    REQUIRE_FALSE(pngBytes.empty());

    const auto decodedOpt = voxels::TextureLoader::LoadFromMemory(pngBytes);
    REQUIRE(decodedOpt.has_value());

    const voxels::ImageData& decoded = *decodedOpt;
    REQUIRE(decoded.width == original.width);
    REQUIRE(decoded.height == original.height);
    REQUIRE(decoded.channels == 4);
    REQUIRE(decoded.pixels == original.pixels);
}

TEST_CASE("TextureLoader.ValidationRules", "[assets][loader]") {
    // Non-power-of-two rejection
    voxels::ImageData nonPot;
    nonPot.width = 15;
    nonPot.height = 15;
    nonPot.channels = 4;
    nonPot.pixels.assign(15 * 15 * 4, 255);
    const auto pngNonPot = voxels::TextureLoader::EncodePngToMemory(
        nonPot.width, nonPot.height, nonPot.channels, nonPot.pixels.data());
    REQUIRE_FALSE(voxels::TextureLoader::LoadFromMemory(pngNonPot, true, true).has_value());

    // Non-square rejection
    voxels::ImageData nonSquare;
    nonSquare.width = 16;
    nonSquare.height = 32;
    nonSquare.channels = 4;
    nonSquare.pixels.assign(16 * 32 * 4, 255);
    const auto pngNonSquare = voxels::TextureLoader::EncodePngToMemory(
        nonSquare.width, nonSquare.height, nonSquare.channels, nonSquare.pixels.data());
    REQUIRE_FALSE(voxels::TextureLoader::LoadFromMemory(pngNonSquare, true, true).has_value());
}

