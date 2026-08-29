/**
 * @file logger.cpp
 * @brief Implementation of the thread-safe leveled logger and built-in sinks.
 *
 * @details See voxels/core/logger.hpp for the architectural overview.
 */

#include "voxels/core/logger.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace voxels {

namespace {

std::string FormatRecord(LogLevel level, std::string_view message) {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuffer{};
#if defined(_WIN32)
    localtime_s(&tmBuffer, &time);
#else
    localtime_r(&time, &tmBuffer);
#endif

    std::ostringstream oss;
    oss << '[' << std::put_time(&tmBuffer, "%Y-%m-%d %H:%M:%S") << "] ["
        << ToString(level) << "] " << message;
    return oss.str();
}

const char* AnsiColorFor(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace: return "\x1b[90m";
        case LogLevel::Debug: return "\x1b[36m";
        case LogLevel::Info:  return "\x1b[32m";
        case LogLevel::Warn:  return "\x1b[33m";
        case LogLevel::Error: return "\x1b[31m";
        case LogLevel::Fatal: return "\x1b[41m";
    }
    return "\x1b[0m";
}

constexpr const char* kAnsiReset = "\x1b[0m";

void EnablePlatformColorSupport() noexcept {
#if defined(_WIN32)
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode)) {
        return;
    }
    SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

} // namespace

std::string_view ToString(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
    }
    return "UNKNOWN";
}

ConsoleLogSink::ConsoleLogSink() {
    EnablePlatformColorSupport();
}

void ConsoleLogSink::Write(LogLevel level, std::string_view formattedMessage) {
    std::ostream& stream = (level >= LogLevel::Error) ? std::cerr : std::cout;
    stream << AnsiColorFor(level) << formattedMessage << kAnsiReset << '\n';
}

FileLogSink::FileLogSink(const std::filesystem::path& logFilePath) {
    if (logFilePath.has_parent_path()) {
        std::filesystem::create_directories(logFilePath.parent_path());
    }
    m_stream.open(logFilePath, std::ios::out | std::ios::app);
}

void FileLogSink::Write(LogLevel level, std::string_view formattedMessage) {
    (void)level;
    std::lock_guard<std::mutex> lock(m_streamMutex);
    if (m_stream.is_open()) {
        m_stream << formattedMessage << '\n';
        m_stream.flush();
    }
}

Logger::Logger(LogLevel minLevel) : m_minLevel(minLevel) {}

void Logger::SetLevel(LogLevel level) noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_minLevel = level;
}

LogLevel Logger::GetLevel() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_minLevel;
}

void Logger::AddSink(std::shared_ptr<ILogSink> sink) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sinks.push_back(std::move(sink));
}

void Logger::ClearSinks() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sinks.clear();
}

void Logger::Log(LogLevel level, std::string_view message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (level < m_minLevel) {
        return;
    }
    const std::string formatted = FormatRecord(level, message);
    for (auto& sink : m_sinks) {
        sink->Write(level, formatted);
    }
}

} // namespace voxels
