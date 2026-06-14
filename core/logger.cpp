module;

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

module vaultium_core_logger;

namespace vaultium {
    namespace {

        [[nodiscard]] auto currentTimestamp() -> std::string
        {
            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);

            std::tm localTime {};

#if defined(VAULTIUM_PLATFORM_WINDOWS)
            localtime_s(&localTime, &time);
#else
            localtime_r(&time, &localTime);
#endif

            std::ostringstream stream;
            stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");

            return stream.str();
        }

        [[nodiscard]] auto levelName(LogLevel level) -> std::string_view
        {
            switch (level) {
                case LogLevel::Info:
                    return "INFO";
                case LogLevel::Warning:
                    return "WARNING";
                case LogLevel::Error:
                    return "ERROR";
                case LogLevel::Success:
                    return "SUCCESS";
            }

            return "UNKNOWN";
        }

    } // namespace

    auto Logger::log(LogLevel level, const std::string& message) -> void
    {
        // All log output goes to stderr so stdout is reserved for program data
        // (e.g. the `inspect` command's JSON). The GUI captures both streams.
        auto& output = std::cerr;

        output
            << '[' << currentTimestamp() << "] "
            << '[' << levelName(level) << "] "
            << message
            << '\n';
    }

    auto Logger::info(const std::string& message) -> void
    {
        log(LogLevel::Info, message);
    }

    auto Logger::warning(const std::string& message) -> void
    {
        log(LogLevel::Warning, message);
    }

    auto Logger::error(const std::string& message) -> void
    {
        log(LogLevel::Error, message);
    }

    auto Logger::success(const std::string& message) -> void
    {
        log(LogLevel::Success, message);
    }

} // namespace vaultium