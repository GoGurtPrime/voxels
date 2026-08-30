/**
 * @file preferences.cpp
 * @brief JSON serialization and platform constraint enforcement for `GamePreferences`.
 *
 * @details Implements a small self-contained JSON reader/writer scoped to the
 *          `GamePreferences` schema, avoiding a general-purpose JSON dependency for a
 *          well-known, flat configuration format.
 */

#include "voxels/core/preferences.hpp"

#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace voxels {

namespace {

std::string_view ToString(WindowMode mode) noexcept {
    switch (mode) {
        case WindowMode::Windowed: return "Windowed";
        case WindowMode::Borderless: return "Borderless";
        case WindowMode::Fullscreen: return "Fullscreen";
    }
    return "Windowed";
}

WindowMode WindowModeFromString(const std::string& value) noexcept {
    if (value == "Borderless") return WindowMode::Borderless;
    if (value == "Fullscreen") return WindowMode::Fullscreen;
    return WindowMode::Windowed;
}

std::string_view ToString(ShadowQuality quality) noexcept {
    switch (quality) {
        case ShadowQuality::Off: return "Off";
        case ShadowQuality::Low: return "Low";
        case ShadowQuality::Medium: return "Medium";
        case ShadowQuality::High: return "High";
    }
    return "Medium";
}

ShadowQuality ShadowQualityFromString(const std::string& value) noexcept {
    if (value == "Off") return ShadowQuality::Off;
    if (value == "Low") return ShadowQuality::Low;
    if (value == "High") return ShadowQuality::High;
    return ShadowQuality::Medium;
}

void WriteJsonString(std::ostringstream& out, std::string_view value) {
    out << '"';
    for (const char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            default: out << c; break;
        }
    }
    out << '"';
}

/// Minimal JSON DOM sufficient for the flat GamePreferences schema (objects, strings, numbers,
/// booleans, and null); arrays are intentionally unsupported as the schema never needs them.
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Object } type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::map<std::string, JsonValue> objectValue;

    [[nodiscard]] std::string GetString(const std::string& key, const std::string& fallback) const {
        const auto it = objectValue.find(key);
        if (it != objectValue.end() && it->second.type == Type::String) {
            return it->second.stringValue;
        }
        return fallback;
    }

    [[nodiscard]] bool GetBool(const std::string& key, bool fallback) const {
        const auto it = objectValue.find(key);
        if (it != objectValue.end() && it->second.type == Type::Bool) {
            return it->second.boolValue;
        }
        return fallback;
    }

    [[nodiscard]] int GetInt(const std::string& key, int fallback) const {
        const auto it = objectValue.find(key);
        if (it != objectValue.end() && it->second.type == Type::Number) {
            return static_cast<int>(it->second.numberValue);
        }
        return fallback;
    }

    [[nodiscard]] float GetFloat(const std::string& key, float fallback) const {
        const auto it = objectValue.find(key);
        if (it != objectValue.end() && it->second.type == Type::Number) {
            return static_cast<float>(it->second.numberValue);
        }
        return fallback;
    }

    [[nodiscard]] const JsonValue* GetObject(const std::string& key) const {
        const auto it = objectValue.find(key);
        if (it != objectValue.end() && it->second.type == Type::Object) {
            return &it->second;
        }
        return nullptr;
    }
};

/// Recursive-descent parser for the JSON subset produced by `PreferencesManager::ToJson`.
class JsonParser {
public:
    explicit JsonParser(const std::string& text) : m_text(text) {}

    [[nodiscard]] JsonValue Parse() {
        SkipWhitespace();
        JsonValue value = ParseValue();
        return value;
    }

private:
    const std::string& m_text;
    std::size_t m_pos = 0;

    [[nodiscard]] char Peek() const {
        if (m_pos >= m_text.size()) {
            throw std::runtime_error("Unexpected end of JSON input");
        }
        return m_text[m_pos];
    }

    char Next() { return m_text[m_pos++]; }

    void SkipWhitespace() {
        while (m_pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_pos]))) {
            ++m_pos;
        }
    }

    void Expect(char expected) {
        if (Peek() != expected) {
            throw std::runtime_error("Malformed JSON: expected character not found");
        }
        ++m_pos;
    }

    JsonValue ParseValue() {
        SkipWhitespace();
        const char c = Peek();
        if (c == '{') return ParseObject();
        if (c == '"') return ParseString();
        if (c == 't' || c == 'f') return ParseBool();
        if (c == 'n') return ParseNull();
        return ParseNumber();
    }

    JsonValue ParseObject() {
        JsonValue value;
        value.type = JsonValue::Type::Object;
        Expect('{');
        SkipWhitespace();
        if (Peek() == '}') {
            ++m_pos;
            return value;
        }
        while (true) {
            SkipWhitespace();
            JsonValue key = ParseString();
            SkipWhitespace();
            Expect(':');
            JsonValue val = ParseValue();
            value.objectValue.emplace(key.stringValue, std::move(val));
            SkipWhitespace();
            if (Peek() == ',') {
                ++m_pos;
                continue;
            }
            Expect('}');
            break;
        }
        return value;
    }

    JsonValue ParseString() {
        JsonValue value;
        value.type = JsonValue::Type::String;
        Expect('"');
        std::string result;
        while (Peek() != '"') {
            char c = Next();
            if (c == '\\') {
                const char escaped = Next();
                switch (escaped) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case 'n': result += '\n'; break;
                    default: result += escaped; break;
                }
            } else {
                result += c;
            }
        }
        Expect('"');
        value.stringValue = std::move(result);
        return value;
    }

    JsonValue ParseBool() {
        JsonValue value;
        value.type = JsonValue::Type::Bool;
        if (m_text.compare(m_pos, 4, "true") == 0) {
            value.boolValue = true;
            m_pos += 4;
        } else if (m_text.compare(m_pos, 5, "false") == 0) {
            value.boolValue = false;
            m_pos += 5;
        } else {
            throw std::runtime_error("Malformed JSON boolean literal");
        }
        return value;
    }

    JsonValue ParseNull() {
        JsonValue value;
        value.type = JsonValue::Type::Null;
        if (m_text.compare(m_pos, 4, "null") != 0) {
            throw std::runtime_error("Malformed JSON null literal");
        }
        m_pos += 4;
        return value;
    }

    JsonValue ParseNumber() {
        const std::size_t start = m_pos;
        if (Peek() == '-') ++m_pos;
        while (m_pos < m_text.size() &&
               (std::isdigit(static_cast<unsigned char>(m_text[m_pos])) || m_text[m_pos] == '.' ||
                m_text[m_pos] == 'e' || m_text[m_pos] == 'E' || m_text[m_pos] == '+' ||
                m_text[m_pos] == '-')) {
            ++m_pos;
        }
        JsonValue value;
        value.type = JsonValue::Type::Number;
        value.numberValue = std::stod(m_text.substr(start, m_pos - start));
        return value;
    }
};

} // namespace

PreferencesManager::PreferencesManager(std::filesystem::path configPath, PlatformType platform)
    : m_configPath(std::move(configPath)), m_platform(platform) {}

GamePreferences PreferencesManager::ApplyPlatformConstraints(GamePreferences preferences,
                                                               PlatformType platform) {
    if (platform == PlatformType::Dreamcast) {
        // The Dreamcast target has no windowing system to toggle and a fixed display mode.
        preferences.windowMode = WindowMode::Fullscreen;
        preferences.resolution = Resolution{640, 480, 60};
    }
    return preferences;
}

std::string PreferencesManager::ToJson(const GamePreferences& preferences) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"windowMode\": ";
    WriteJsonString(out, ToString(preferences.windowMode));
    out << ",\n";
    out << "  \"resolution\": {\n";
    out << "    \"width\": " << preferences.resolution.width << ",\n";
    out << "    \"height\": " << preferences.resolution.height << ",\n";
    out << "    \"refreshRate\": " << preferences.resolution.refreshRate << "\n";
    out << "  },\n";
    out << "  \"renderDistance\": " << preferences.renderDistance << ",\n";
    out << "  \"simulationDistance\": " << preferences.simulationDistance << ",\n";
    out << "  \"fieldOfView\": " << preferences.fieldOfView << ",\n";
    out << "  \"mouseSensitivity\": " << preferences.mouseSensitivity << ",\n";
    out << "  \"invertY\": " << (preferences.invertY ? "true" : "false") << ",\n";
    out << "  \"antiAliasingSamples\": " << preferences.antiAliasingSamples << ",\n";
    out << "  \"shadowQuality\": ";
    WriteJsonString(out, ToString(preferences.shadowQuality));
    out << ",\n";
    out << "  \"masterVolume\": " << preferences.masterVolume << ",\n";
    out << "  \"musicVolume\": " << preferences.musicVolume << ",\n";
    out << "  \"sfxVolume\": " << preferences.sfxVolume << ",\n";
    out << "  \"keyBindings\": {\n";
    std::size_t index = 0;
    for (const auto& [action, binding] : preferences.keyBindings) {
        out << "    ";
        WriteJsonString(out, action);
        out << ": ";
        WriteJsonString(out, binding);
        if (++index != preferences.keyBindings.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  }\n";
    out << "}\n";
    return out.str();
}

GamePreferences PreferencesManager::FromJson(const std::string& json) {
    const JsonValue root = JsonParser(json).Parse();

    GamePreferences preferences;
    preferences.windowMode = WindowModeFromString(root.GetString("windowMode", "Windowed"));
    if (const JsonValue* resolution = root.GetObject("resolution")) {
        preferences.resolution.width = resolution->GetInt("width", preferences.resolution.width);
        preferences.resolution.height = resolution->GetInt("height", preferences.resolution.height);
        preferences.resolution.refreshRate =
            resolution->GetInt("refreshRate", preferences.resolution.refreshRate);
    }
    preferences.renderDistance = root.GetInt("renderDistance", preferences.renderDistance);
    preferences.simulationDistance = root.GetInt("simulationDistance", preferences.simulationDistance);
    preferences.fieldOfView = root.GetFloat("fieldOfView", preferences.fieldOfView);
    preferences.mouseSensitivity = root.GetFloat("mouseSensitivity", preferences.mouseSensitivity);
    preferences.invertY = root.GetBool("invertY", preferences.invertY);
    preferences.antiAliasingSamples = root.GetInt("antiAliasingSamples", preferences.antiAliasingSamples);
    preferences.shadowQuality = ShadowQualityFromString(root.GetString("shadowQuality", "Medium"));
    preferences.masterVolume = root.GetFloat("masterVolume", preferences.masterVolume);
    preferences.musicVolume = root.GetFloat("musicVolume", preferences.musicVolume);
    preferences.sfxVolume = root.GetFloat("sfxVolume", preferences.sfxVolume);
    if (const JsonValue* keyBindings = root.GetObject("keyBindings")) {
        for (const auto& [action, binding] : keyBindings->objectValue) {
            if (binding.type == JsonValue::Type::String) {
                preferences.keyBindings.emplace(action, binding.stringValue);
            }
        }
    }
    return preferences;
}

GamePreferences PreferencesManager::Load() const {
    std::ifstream file(m_configPath);
    if (!file.is_open()) {
        return ApplyPlatformConstraints(GamePreferences{}, m_platform);
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return ApplyPlatformConstraints(FromJson(buffer.str()), m_platform);
}

void PreferencesManager::Save(const GamePreferences& preferences) {
    const GamePreferences constrained = ApplyPlatformConstraints(preferences, m_platform);
    if (m_configPath.has_parent_path()) {
        std::filesystem::create_directories(m_configPath.parent_path());
    }
    std::ofstream file(m_configPath, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open preferences file for writing: " +
                                  m_configPath.string());
    }
    file << ToJson(constrained);
}

} // namespace voxels
