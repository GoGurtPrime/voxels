/**
 * @file texture_forge.cpp
 * @brief Implementation of procedural texture synthesis for launch block assets.
 */

#include "voxels/render/texture_forge.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "voxels/assets/texture_loader.hpp"
#include "voxels/core/logger.hpp"

namespace voxels {

namespace {

struct ColorRGBA {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

inline std::uint32_t HashCoords(int x, int y, std::uint32_t seed) noexcept {
    std::uint32_t h = seed ^ (static_cast<std::uint32_t>(x) * 374761393U) ^ (static_cast<std::uint32_t>(y) * 668265263U);
    h = (h ^ (h >> 13U)) * 1274126177U;
    return h ^ (h >> 16U);
}

inline float Noise2D(int x, int y, std::uint32_t seed) noexcept {
    return static_cast<float>(HashCoords(x, y, seed) & 0xFFFFU) / 65535.0f;
}

ColorRGBA LerpColor(const ColorRGBA& c1, const ColorRGBA& c2, float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    return ColorRGBA{
        static_cast<std::uint8_t>(c1.r + t * (c2.r - c1.r)),
        static_cast<std::uint8_t>(c1.g + t * (c2.g - c1.g)),
        static_cast<std::uint8_t>(c1.b + t * (c2.b - c1.b)),
        static_cast<std::uint8_t>(c1.a + t * (c2.a - c1.a))
    };
}

void SetPixel(ImageData& img, int x, int y, const ColorRGBA& c) noexcept {
    if (x < 0 || x >= img.width || y < 0 || y >= img.height) {
        return;
    }
    const std::size_t idx = (static_cast<std::size_t>(y) * img.width + x) * 4U;
    img.pixels[idx + 0] = c.r;
    img.pixels[idx + 1] = c.g;
    img.pixels[idx + 2] = c.b;
    img.pixels[idx + 3] = c.a;
}

ColorRGBA GetPixel(const ImageData& img, int x, int y) noexcept {
    if (x < 0 || x >= img.width || y < 0 || y >= img.height) {
        return {};
    }
    const std::size_t idx = (static_cast<std::size_t>(y) * img.width + x) * 4U;
    return ColorRGBA{img.pixels[idx], img.pixels[idx + 1], img.pixels[idx + 2], img.pixels[idx + 3]};
}

ImageData CreateEmptyImage(int w = 16, int h = 16) {
    ImageData img;
    img.width = w;
    img.height = h;
    img.channels = 4;
    img.pixels.assign(static_cast<std::size_t>(w * h * 4), 255);
    return img;
}

ImageData CreateTransparentImage(int w = 16, int h = 16) {
    ImageData img;
    img.width = w;
    img.height = h;
    img.channels = 4;
    img.pixels.assign(static_cast<std::size_t>(w * h * 4), 0);
    return img;
}

void DrawAlphaLine(ImageData& img, int x0, int y0, int x1, int y1, std::uint8_t alpha) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        SetPixel(img, x0, y0, {24, 12, 8, alpha});
        if (x0 == x1 && y0 == y1) break;
        const int doubledError = 2 * error;
        if (doubledError >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubledError <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

ImageData ForgeCrack(int stage) {
    ImageData img = CreateTransparentImage();
    const std::array<std::array<int, 4>, 10> segments = {{{2, 1, 8, 6}, {8, 6, 13, 5}, {8, 6, 7, 12},
                                                            {7, 12, 3, 14}, {7, 12, 12, 14}, {4, 3, 3, 8},
                                                            {11, 1, 13, 5}, {1, 10, 4, 11}, {12, 8, 14, 10},
                                                            {6, 7, 2, 6}}};
    const int segmentCount = std::clamp(stage + 1, 1, static_cast<int>(segments.size()));
    const std::uint8_t alpha = static_cast<std::uint8_t>(80 + segmentCount * 17);
    for (int index = 0; index < segmentCount; ++index) {
        const auto& segment = segments[static_cast<std::size_t>(index)];
        DrawAlphaLine(img, segment[0], segment[1], segment[2], segment[3], alpha);
    }
    return img;
}

ImageData ForgeCrosshair() {
    ImageData img = CreateTransparentImage();
    for (int offset = 0; offset < 5; ++offset) {
        SetPixel(img, 3 + offset, 7, {255, 255, 255, 230});
        SetPixel(img, 8 + offset, 7, {255, 255, 255, 230});
        SetPixel(img, 7, 3 + offset, {255, 255, 255, 230});
        SetPixel(img, 7, 8 + offset, {255, 255, 255, 230});
    }
    return img;
}

ImageData ForgeHotbarFrame(bool selected) {
    const int width = selected ? 24 : 182;
    const int height = selected ? 24 : 22;
    ImageData img = CreateTransparentImage(width, height);
    const ColorRGBA border = selected ? ColorRGBA{255, 209, 70, 255} : ColorRGBA{30, 25, 21, 235};
    for (int x = 0; x < width; ++x) {
        SetPixel(img, x, 0, border);
        SetPixel(img, x, height - 1, border);
    }
    for (int y = 0; y < height; ++y) {
        SetPixel(img, 0, y, border);
        SetPixel(img, width - 1, y, border);
    }
    if (!selected) {
        for (int slot = 1; slot < 9; ++slot) {
            const int x = slot * 20 + 1;
            for (int y = 1; y < height - 1; ++y) SetPixel(img, x, y, border);
        }
    }
    return img;
}

ImageData ForgeStone(std::uint32_t seed) {
    ImageData img = CreateEmptyImage();
    const ColorRGBA baseDark{95, 95, 95, 255};
    const ColorRGBA baseMid{128, 128, 128, 255};
    const ColorRGBA baseLight{155, 155, 155, 255};
    const ColorRGBA fleckDark{75, 75, 75, 255};

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const float n1 = Noise2D(x, y, seed);
            const float n2 = Noise2D(x, y, seed + 101U);
            ColorRGBA c = LerpColor(baseDark, baseMid, n1);
            if (n2 > 0.65f) {
                c = LerpColor(c, baseLight, (n2 - 0.65f) / 0.35f);
            } else if (n2 < 0.15f) {
                c = fleckDark;
            }
            SetPixel(img, x, y, c);
        }
    }
    return img;
}

ImageData ForgeDirt(std::uint32_t seed) {
    ImageData img = CreateEmptyImage();
    const ColorRGBA darkBrown{92, 62, 42, 255};
    const ColorRGBA midBrown{134, 96, 67, 255};
    const ColorRGBA lightBrown{165, 122, 88, 255};
    const ColorRGBA pebbleColor{118, 112, 102, 255};

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const float n1 = Noise2D(x, y, seed + 200U);
            const float n2 = Noise2D(x, y, seed + 250U);
            ColorRGBA c = LerpColor(darkBrown, midBrown, n1);
            if (n2 > 0.82f) {
                c = lightBrown;
            } else if (n2 < 0.08f) {
                c = pebbleColor;
            }
            SetPixel(img, x, y, c);
        }
    }
    return img;
}

ImageData ForgeGrassTop(std::uint32_t seed) {
    ImageData img = CreateEmptyImage();
    const ColorRGBA darkGreen{72, 120, 42, 255};
    const ColorRGBA midGreen{95, 155, 55, 255};
    const ColorRGBA brightGreen{118, 185, 68, 255};

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const float n1 = Noise2D(x, y, seed + 300U);
            const float n2 = Noise2D(x, y, seed + 350U);
            ColorRGBA c = LerpColor(darkGreen, midGreen, n1);
            if (n2 > 0.70f) {
                c = LerpColor(c, brightGreen, (n2 - 0.70f) / 0.30f);
            }
            SetPixel(img, x, y, c);
        }
    }
    return img;
}

ImageData ForgeGrassSide(std::uint32_t seed) {
    ImageData img = ForgeDirt(seed);
    const ColorRGBA grassDark{72, 120, 42, 255};
    const ColorRGBA grassMid{95, 155, 55, 255};
    const ColorRGBA grassBright{118, 185, 68, 255};
    const ColorRGBA grassShadow{50, 35, 22, 255};

    for (int x = 0; x < 16; ++x) {
        const float overhangNoise = Noise2D(x, 0, seed + 400U);
        const int hang = 2 + static_cast<int>(overhangNoise * 2.99f); // 2..4 pixels depth

        for (int y = 0; y <= hang; ++y) {
            const float gn = Noise2D(x, y, seed + 450U);
            ColorRGBA c = (y == 0) ? grassBright : LerpColor(grassDark, grassMid, gn);
            SetPixel(img, x, y, c);
        }
        if (hang + 1 < 16) {
            SetPixel(img, x, hang + 1, grassShadow);
        }
    }
    return img;
}

ImageData ForgeSand(std::uint32_t seed) {
    ImageData img = CreateEmptyImage();
    const ColorRGBA sandDark{196, 180, 132, 255};
    const ColorRGBA sandMid{222, 208, 156, 255};
    const ColorRGBA sandLight{238, 228, 182, 255};

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const float ripple = std::sin((x * 0.7f + y * 0.3f)) * 0.1f;
            const float n = std::clamp(Noise2D(x, y, seed + 500U) + ripple, 0.0f, 1.0f);
            ColorRGBA c = LerpColor(sandDark, sandMid, n);
            if (n > 0.75f) {
                c = LerpColor(c, sandLight, (n - 0.75f) / 0.25f);
            }
            SetPixel(img, x, y, c);
        }
    }
    return img;
}

ImageData ForgeGravel(std::uint32_t seed) {
    ImageData img = CreateEmptyImage();
    const ColorRGBA mortarDark{65, 65, 70, 255};
    const ColorRGBA pebbleDark{95, 95, 100, 255};
    const ColorRGBA pebbleMid{135, 135, 140, 255};
    const ColorRGBA pebbleLight{168, 168, 172, 255};

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const float n1 = Noise2D(x, y, seed + 600U);
            const float n2 = Noise2D(x, y, seed + 650U);
            ColorRGBA c = (n1 < 0.2f) ? mortarDark : LerpColor(pebbleDark, pebbleMid, n2);
            if (n1 > 0.85f) {
                c = pebbleLight;
            }
            SetPixel(img, x, y, c);
        }
    }
    return img;
}

ImageData ForgeWater(std::uint32_t seed) {
    ImageData img = CreateEmptyImage();
    const ColorRGBA waterDeep{35, 85, 160, 205};
    const ColorRGBA waterMid{55, 115, 195, 210};
    const ColorRGBA waterGlint{100, 175, 248, 235};

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const float wave = std::sin((x * 0.6f + y * 0.8f)) * 0.5f + 0.5f;
            const float n = Noise2D(x, y, seed + 700U);
            ColorRGBA c = LerpColor(waterDeep, waterMid, wave * 0.7f + n * 0.3f);
            if (wave > 0.85f && n > 0.5f) {
                c = waterGlint;
            }
            SetPixel(img, x, y, c);
        }
    }
    return img;
}

ImageData ForgeCoalOre(std::uint32_t seed) {
    ImageData img = ForgeStone(seed);
    const ColorRGBA coalDark{18, 18, 20, 255};
    const ColorRGBA coalMid{36, 36, 42, 255};
    const ColorRGBA coalHighlight{65, 65, 75, 255};

    // Place 3 clusters of coal flecks
    const std::array<std::pair<int, int>, 3> spots = {{{3, 4}, {10, 5}, {6, 11}}};
    for (const auto& [cx, cy] : spots) {
        for (int dy = -1; dy <= 2; ++dy) {
            for (int dx = -1; dx <= 2; ++dx) {
                const int px = (cx + dx + 16) % 16;
                const int py = (cy + dy + 16) % 16;
                const float n = Noise2D(px, py, seed + 800U);
                if (n > 0.35f) {
                    ColorRGBA c = (n > 0.75f) ? coalHighlight : ((n > 0.5f) ? coalMid : coalDark);
                    SetPixel(img, px, py, c);
                }
            }
        }
    }
    return img;
}

ImageData ForgeIronOre(std::uint32_t seed) {
    ImageData img = ForgeStone(seed);
    const ColorRGBA ironDark{165, 95, 50, 255};
    const ColorRGBA ironMid{215, 160, 115, 255};
    const ColorRGBA ironLight{245, 205, 170, 255};

    const std::array<std::pair<int, int>, 3> spots = {{{4, 11}, {11, 4}, {8, 7}}};
    for (const auto& [cx, cy] : spots) {
        for (int dy = -1; dy <= 2; ++dy) {
            for (int dx = -1; dx <= 2; ++dx) {
                const int px = (cx + dx + 16) % 16;
                const int py = (cy + dy + 16) % 16;
                const float n = Noise2D(px, py, seed + 900U);
                if (n > 0.35f) {
                    ColorRGBA c = (n > 0.75f) ? ironLight : ((n > 0.5f) ? ironMid : ironDark);
                    SetPixel(img, px, py, c);
                }
            }
        }
    }
    return img;
}

ImageData ForgeWoodLogTop(std::uint32_t seed) {
    ImageData img = CreateEmptyImage();
    const ColorRGBA barkColor{74, 55, 40, 255};
    const ColorRGBA innerBark{108, 82, 58, 255};
    const ColorRGBA ringLight{168, 138, 102, 255};
    const ColorRGBA ringDark{138, 108, 76, 255};
    const ColorRGBA pithColor{118, 90, 62, 255};

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const float dx = (x - 7.5f);
            const float dy = (y - 7.5f);
            const float r = std::sqrt(dx * dx + dy * dy);
            const float n = Noise2D(x, y, seed + 1000U) * 0.4f;

            ColorRGBA c;
            if (r >= 6.4f) {
                c = barkColor;
            } else if (r >= 5.5f) {
                c = innerBark;
            } else if (r <= 1.5f) {
                c = pithColor;
            } else {
                const float ringVal = std::sin((r + n) * 3.14159f * 1.5f);
                c = LerpColor(ringDark, ringLight, ringVal * 0.5f + 0.5f);
            }
            SetPixel(img, x, y, c);
        }
    }
    return img;
}

ImageData ForgeWoodLogSide(std::uint32_t seed) {
    ImageData img = CreateEmptyImage();
    const ColorRGBA barkDark{54, 38, 28, 255};
    const ColorRGBA barkMid{78, 58, 42, 255};
    const ColorRGBA barkLight{102, 78, 56, 255};

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const float verticalStripe = Noise2D(x, 0, seed + 1100U);
            const float detail = Noise2D(x, y, seed + 1150U);
            ColorRGBA c = LerpColor(barkDark, barkMid, verticalStripe * 0.7f + detail * 0.3f);
            if (detail > 0.8f) {
                c = barkLight;
            } else if (detail < 0.15f) {
                c = barkDark;
            }
            SetPixel(img, x, y, c);
        }
    }
    return img;
}

ImageData ForgeLeaves(std::uint32_t seed) {
    ImageData img = CreateEmptyImage();
    const ColorRGBA leafDark{42, 98, 42, 255};
    const ColorRGBA leafMid{62, 138, 56, 255};
    const ColorRGBA leafLight{88, 172, 76, 255};
    const ColorRGBA transparent{0, 0, 0, 0};

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const float n1 = Noise2D(x, y, seed + 1200U);
            const float n2 = Noise2D(x, y, seed + 1250U);
            if (n1 < 0.18f) {
                SetPixel(img, x, y, transparent);
            } else {
                ColorRGBA c = LerpColor(leafDark, leafMid, n2);
                if (n1 > 0.75f) {
                    c = leafLight;
                }
                SetPixel(img, x, y, c);
            }
        }
    }
    return img;
}

ImageData ForgePlanks(std::uint32_t seed) {
    ImageData img = CreateEmptyImage();
    const ColorRGBA seamDark{98, 68, 38, 255};
    const ColorRGBA plankLight{182, 142, 96, 255};
    const ColorRGBA plankMid{158, 120, 78, 255};
    const ColorRGBA nailColor{72, 52, 32, 255};

    for (int y = 0; y < 16; ++y) {
        const int plankIndex = y / 4;
        const int rowInPlank = y % 4;
        const float plankOffset = Noise2D(plankIndex, 0, seed + 1300U) * 0.3f;

        for (int x = 0; x < 16; ++x) {
            if (rowInPlank == 3) {
                SetPixel(img, x, y, seamDark);
                continue;
            }

            const float grain = Noise2D(x, y, seed + 1350U) * 0.4f;
            ColorRGBA c = LerpColor(plankMid, plankLight, grain + plankOffset);

            // Nail details on left/right edges of planks
            if ((x == 1 || x == 14) && (rowInPlank == 1)) {
                c = nailColor;
            }

            SetPixel(img, x, y, c);
        }
    }
    return img;
}

ImageData ForgeGlass(std::uint32_t /*seed*/) {
    ImageData img = CreateEmptyImage();
    const ColorRGBA frameColor{175, 230, 245, 255};
    const ColorRGBA glintColor{240, 252, 255, 210};
    const ColorRGBA clearInterior{210, 235, 248, 35};

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            if (x == 0 || x == 15 || y == 0 || y == 15) {
                SetPixel(img, x, y, frameColor);
            } else if ((x + y >= 4 && x + y <= 5) || (x + y >= 19 && x + y <= 20)) {
                SetPixel(img, x, y, glintColor);
            } else {
                SetPixel(img, x, y, clearInterior);
            }
        }
    }
    return img;
}

ImageData ForgeBedrock(std::uint32_t seed) {
    ImageData img = CreateEmptyImage();
    const ColorRGBA black{14, 14, 18, 255};
    const ColorRGBA darkSlate{38, 38, 44, 255};
    const ColorRGBA midSlate{68, 68, 76, 255};
    const ColorRGBA lightFleck{105, 105, 115, 255};

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const float n = Noise2D(x, y, seed + 1400U);
            ColorRGBA c;
            if (n < 0.35f) {
                c = black;
            } else if (n < 0.70f) {
                c = darkSlate;
            } else if (n < 0.92f) {
                c = midSlate;
            } else {
                c = lightFleck;
            }
            SetPixel(img, x, y, c);
        }
    }
    return img;
}

} // namespace

ImageData TextureForge::GenerateMissingTexture(int width, int height) {
    ImageData img;
    img.width = width;
    img.height = height;
    img.channels = 4;
    img.pixels.resize(static_cast<std::size_t>(width * height * 4));

    const ColorRGBA magenta{255, 0, 255, 255};
    const ColorRGBA black{0, 0, 0, 255};
    const int halfW = std::max(1, width / 2);
    const int halfH = std::max(1, height / 2);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool checker = ((x / halfW) + (y / halfH)) % 2 == 0;
            SetPixel(img, x, y, checker ? magenta : black);
        }
    }
    return img;
}

ImageData TextureForge::GenerateCrackTexture(int stage) {
    return ForgeCrack(stage);
}

std::vector<std::string> TextureForge::GetLaunchTextureNames() {
    return {
        "stone",
        "dirt",
        "grass_top",
        "grass_side",
        "sand",
        "gravel",
        "water",
        "coal_ore",
        "iron_ore",
        "wood_log_top",
        "wood_log_side",
        "leaves",
        "planks",
        "glass",
        "bedrock"
    };
}

ImageData TextureForge::GenerateTexture(std::string_view textureName, std::uint32_t seed) {
    if (textureName == "blocks/stone" || textureName == "stone") return ForgeStone(seed);
    if (textureName == "blocks/dirt" || textureName == "dirt") return ForgeDirt(seed);
    if (textureName == "blocks/grass_top" || textureName == "grass_top") return ForgeGrassTop(seed);
    if (textureName == "blocks/grass_side" || textureName == "grass_side") return ForgeGrassSide(seed);
    if (textureName == "blocks/sand" || textureName == "sand") return ForgeSand(seed);
    if (textureName == "blocks/gravel" || textureName == "gravel") return ForgeGravel(seed);
    if (textureName == "blocks/water" || textureName == "water") return ForgeWater(seed);
    if (textureName == "blocks/coal_ore" || textureName == "coal_ore") return ForgeCoalOre(seed);
    if (textureName == "blocks/iron_ore" || textureName == "iron_ore") return ForgeIronOre(seed);
    if (textureName == "blocks/wood_log_top" || textureName == "wood_log_top") return ForgeWoodLogTop(seed);
    if (textureName == "blocks/wood_log_side" || textureName == "wood_log_side") return ForgeWoodLogSide(seed);
    if (textureName == "blocks/leaves" || textureName == "leaves") return ForgeLeaves(seed);
    if (textureName == "blocks/planks" || textureName == "planks") return ForgePlanks(seed);
    if (textureName == "blocks/glass" || textureName == "glass") return ForgeGlass(seed);
    if (textureName == "blocks/bedrock" || textureName == "bedrock") return ForgeBedrock(seed);

    return GenerateMissingTexture(kDefaultTextureSize, kDefaultTextureSize);
}

std::size_t TextureForge::ForgeLaunchTextures(
    const std::filesystem::path& targetDirectory,
    bool overwrite,
    std::uint32_t seed) {
    std::size_t writtenCount = 0;
    std::error_code ec;
    std::filesystem::create_directories(targetDirectory, ec);

    for (const auto& name : GetLaunchTextureNames()) {
        std::filesystem::path outPath = targetDirectory / (name + ".png");
        if (outPath.has_parent_path()) {
            std::filesystem::create_directories(outPath.parent_path(), ec);
        }

        if (!overwrite && std::filesystem::exists(outPath)) {
            continue;
        }

        const ImageData img = GenerateTexture(name, seed);
        if (TextureLoader::WritePngToFile(outPath, img)) {
            ++writtenCount;
        }
    }
    return writtenCount;
}

std::size_t TextureForge::ForgeInteractionAssets(const std::filesystem::path& assetRoot, bool overwrite) {
    const std::array<std::pair<std::filesystem::path, ImageData>, 3> uiAssets = {{
        {"ui/crosshair.png", ForgeCrosshair()},
        {"ui/hotbar.png", ForgeHotbarFrame(false)},
        {"ui/hotbar_selection.png", ForgeHotbarFrame(true)},
    }};

    std::size_t writtenCount = 0;
    const auto writeAsset = [&](const std::filesystem::path& relativePath, const ImageData& image) {
        const std::filesystem::path outputPath = assetRoot / relativePath;
        if (overwrite || !std::filesystem::exists(outputPath)) {
            if (TextureLoader::WritePngToFile(outputPath, image)) ++writtenCount;
        }
    };
    for (const auto& [relativePath, image] : uiAssets) writeAsset(relativePath, image);
    for (int stage = 0; stage < 10; ++stage) {
        writeAsset(std::filesystem::path("textures/misc") / ("crack_" + std::to_string(stage) + ".png"),
                   ForgeCrack(stage));
    }
    return writtenCount;
}

} // namespace voxels
