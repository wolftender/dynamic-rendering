#pragma once
#include <cstdio>
#include <source_location>
#include <string_view>
#include <array>
#include <chrono>

#include <fmt/base.h>
#include <fmt/xchar.h>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace logger {
enum class LogLevel { eDebug = 0, eInfo = 1, eWarning = 2, eError = 3 };
enum class LogColor { eNoColor = 0, eColor = 1 };

constexpr std::array<std::string_view, 4> kLogPrefix = {"[DEBUG]", "[INFO]", "[WARNING]", "[ERROR]"};

constexpr std::array<std::string_view, 4> kLogPrefixColored = {
    "\x1b[34;1m[DEBUG]\x1b[0m", "\x1b[32;1m[INFO]\x1b[0m", "\x1b[33;1m[WARNING]\x1b[0m", "\x1b[31;1m[ERROR]\x1b[0m"};

constexpr std::array<std::wstring_view, 4> kLogPrefixW = {L"[DEBUG]", L"[INFO]", L"[WARNING]", L"[ERROR]"};

constexpr std::array<std::wstring_view, 4> kLogPrefixColoredW = {
    L"\x1b[34;1m[DEBUG]\x1b[0m", L"\x1b[32;1m[INFO]\x1b[0m", L"\x1b[33;1m[WARNING]\x1b[0m",
    L"\x1b[31;1m[ERROR]\x1b[0m"};

constexpr std::string_view logLevelToString(const LogColor &color, const LogLevel &level) {
    const auto &prefixes = color == LogColor::eColor ? kLogPrefixColored : kLogPrefix;
    switch (level) {
    case logger::LogLevel::eDebug:
        return prefixes[0];
    case logger::LogLevel::eInfo:
        return prefixes[1];
    case logger::LogLevel::eWarning:
        return prefixes[2];
    case logger::LogLevel::eError:
        return prefixes[3];
    default:
        return prefixes[0];
    }
}

constexpr std::wstring_view logLevelToStringW(const LogColor &color, const LogLevel &level) {
    const auto &prefixes = color == LogColor::eColor ? kLogPrefixColoredW : kLogPrefixW;
    switch (level) {
    case logger::LogLevel::eDebug:
        return prefixes[0];
    case logger::LogLevel::eInfo:
        return prefixes[1];
    case logger::LogLevel::eWarning:
        return prefixes[2];
    case logger::LogLevel::eError:
        return prefixes[3];
    default:
        return prefixes[0];
    }
}

template <class... Args>
void log(const LogLevel &level, const std::source_location &location, fmt::format_string<Args...> fmt, Args &&...args) {
    constexpr size_t kMaxMsgLen = 2048;
    thread_local std::array<char, kMaxMsgLen + 1> message;
    thread_local std::array<char, kMaxMsgLen + 1> finalMessage;

    auto msgRes = fmt::vformat_to_n(
        message.begin(), kMaxMsgLen, fmt::basic_string_view<char>(fmt),
        fmt::make_format_args<fmt::buffered_context<char>>(args...));
    *msgRes.out = '\0';

    // timestamp
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::system_clock::to_time_t(now);

    std::tm local_tm;

#ifdef _MSC_VER
    localtime_s(&local_tm, &timestamp);
#else
    localtime_r(&timestamp, &local_tm);
#endif

    auto sec_since_epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
    auto fracs = (now.time_since_epoch() - sec_since_epoch).count();

    // print to output debug string (debuggers will see this)
    auto finalRes = fmt::format_to_n(
        finalMessage.begin(), kMaxMsgLen, "[{:02}:{:02}:{:02}.{:06}]{} {} in {} at {}\n", local_tm.tm_hour,
        local_tm.tm_min, local_tm.tm_sec, fracs, logLevelToString(LogColor::eNoColor, level), message.data(),
        location.file_name(), location.line());
    *finalRes.out = '\0';

#if defined(_WIN32) && !defined(ENABLE_STDOUT_LOGGING)
    OutputDebugStringA(finalMessage.data());
#else
    switch (level) {
    case logger::LogLevel::eWarning:
    case logger::LogLevel::eError:
        ::fputs(finalMessage.data(), stderr);
        break;

    case logger::LogLevel::eDebug:
    case logger::LogLevel::eInfo:
    default:
        ::fputs(finalMessage.data(), stdout);
        break;
    }
#endif
}

// wide version only for windows
#ifdef _WIN32
template <class... Args>
void log(
    const LogLevel &level, const wchar_t *sourceFile, const uint32_t line, fmt::wformat_string<Args...> fmt,
    Args &&...args) {
    constexpr size_t kMaxMsgLen = 2048;
    thread_local std::array<wchar_t, kMaxMsgLen + 1> message;
    thread_local std::array<wchar_t, kMaxMsgLen + 1> finalMessage;

    auto msgRes = fmt::vformat_to_n(
        message.begin(), kMaxMsgLen, fmt::basic_string_view<wchar_t>(fmt),
        fmt::make_format_args<fmt::buffered_context<wchar_t>>(args...));
    *msgRes.out = '\0';

    // timestamp
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::system_clock::to_time_t(now);

    std::tm local_tm;

#ifdef _MSC_VER
    localtime_s(&local_tm, &timestamp);
#else
    localtime_r(&timestamp, &local_tm);
#endif

    auto sec_since_epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
    auto fracs = (now.time_since_epoch() - sec_since_epoch).count();

    // print to output debug string (debuggers will see this)
    auto finalRes = fmt::format_to_n(
        finalMessage.begin(), kMaxMsgLen, L"[{:02}:{:02}:{:02}.{:06}]{} {} in {} at {}\n", local_tm.tm_hour,
        local_tm.tm_min, local_tm.tm_sec, fracs, logLevelToStringW(LogColor::eNoColor, level), message.data(),
        sourceFile, line);
    *finalRes.out = '\0';
    OutputDebugStringW(finalMessage.data());
}
#endif
} // namespace logger

#ifndef NDEBUG
#define LogDebug(fmt, ...) logger::log(logger::LogLevel::eDebug, std::source_location::current(), fmt, __VA_ARGS__)
#else
#define LogDebug(fmt, ...)
#endif

#define WIDE2(x) L##x
#define WIDE1(x) WIDE2(x)
#define WFILE WIDE1(__FILE__)

#ifdef ENABLE_LOGGING
#define LogInfo(fmt, ...) logger::log(logger::LogLevel::eInfo, std::source_location::current(), fmt, __VA_ARGS__)

#define LogWarning(fmt, ...) logger::log(logger::LogLevel::eWarning, std::source_location::current(), fmt, __VA_ARGS__)

#define LogError(fmt, ...) logger::log(logger::LogLevel::eError, std::source_location::current(), fmt, __VA_ARGS__)
#else
#define LogInfo(fmt, ...)
#define LogWarning(fmt, ...)
#define LogError(fmt, ...)
#endif

#if defined(_WIN32) && defined(ENABLE_LOGGING)
#define LogInfoW(fmt, ...) logger::log(logger::LogLevel::eInfo, WFILE, __LINE__, fmt, __VA_ARGS__)

#define LogWarningW(fmt, ...) logger::log(logger::LogLevel::eWarning, WFILE, __LINE__, fmt, __VA_ARGS__)

#define LogErrorW(fmt, ...) logger::log(logger::LogLevel::eError, WFILE, __LINE__, fmt, __VA_ARGS__)
#else
#define LogInfoW(fmt, ...)
#define LogWarningW(fmt, ...)
#define LogErrorW(fmt, ...)
#endif

#if defined(_WIN32) && defined(ENABLE_LOGGING) && !defined(NDEBUG)
#define LogDebugW(fmt, ...) logger::log(logger::LogLevel::eDebug, WFILE, __LINE__, fmt, __VA_ARGS__)
#else
#define LogDebugW(fmt, ...)
#endif
