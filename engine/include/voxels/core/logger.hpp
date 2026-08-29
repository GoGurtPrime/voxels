#pragma once

/**
 * @file logger.hpp
 * @brief Thread-safe multi-level logging facility with pluggable sinks.
 *
 * @details Provides a leveled logger (TRACE..FATAL) that fans out formatted records to any
 *          number of registered sinks. Ships with a console sink (with platform color codes)
 *          and a file sink (for `engine.log`). Additional sinks (e.g. in-memory capture for
 *          tests) can be registered without changing the core logging code.
 *
 *          Relation to the rest of the codebase: every subsystem (platform, rendering, world,
 *          networking) is expected to log through a shared or per-module `Logger` instance
 *          rather than writing to stdout directly, keeping output formatting and thread-safety
 *          centralized.
 */

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace voxels {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

[[nodiscard]] std::string_view ToString(LogLevel level) noexcept;

class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void Write(LogLevel level, std::string_view formattedMessage) = 0;
};

/// Writes log records to stdout/stderr, applying ANSI color codes where supported.
class ConsoleLogSink final : public ILogSink {
public:
    ConsoleLogSink();
    void Write(LogLevel level, std::string_view formattedMessage) override;
};

/// Appends log records to a file on disk (e.g. `engine.log`), creating parent directories.
class FileLogSink final : public ILogSink {
public:
    explicit FileLogSink(const std::filesystem::path& logFilePath);
    void Write(LogLevel level, std::string_view formattedMessage) override;

private:
    std::ofstream m_stream;
    std::mutex m_streamMutex;
};

/// Thread-safe leveled logger that dispatches formatted records to registered sinks.
class Logger {
public:
    explicit Logger(LogLevel minLevel = LogLevel::Info);

    void SetLevel(LogLevel level) noexcept;
    [[nodiscard]] LogLevel GetLevel() const noexcept;

    void AddSink(std::shared_ptr<ILogSink> sink);
    void ClearSinks();

    void Log(LogLevel level, std::string_view message);

    void Trace(std::string_view message) { Log(LogLevel::Trace, message); }
    void Debug(std::string_view message) { Log(LogLevel::Debug, message); }
    void Info(std::string_view message) { Log(LogLevel::Info, message); }
    void Warn(std::string_view message) { Log(LogLevel::Warn, message); }
    void Error(std::string_view message) { Log(LogLevel::Error, message); }
    void Fatal(std::string_view message) { Log(LogLevel::Fatal, message); }

private:
    mutable std::mutex m_mutex;
    LogLevel m_minLevel;
    std::vector<std::shared_ptr<ILogSink>> m_sinks;
};

} // namespace voxels
