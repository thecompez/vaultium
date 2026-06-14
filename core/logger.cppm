module;

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

export module vaultium_core_logger;

export namespace vaultium {

    /**
     * @brief Log severity level.
     */
    enum class LogLevel {
        Info,
        Warning,
        Error,
        Success
    };

    /**
     * @brief Simple structured console logger.
     */
    class Logger final {
    public:
        /**
         * @brief Writes a log line.
         *
         * @param level Severity level.
         * @param message Message text.
         */
        static auto log(LogLevel level, const std::string& message) -> void;

        /**
         * @brief Writes an info log line.
         *
         * @param message Message text.
         */
        static auto info(const std::string& message) -> void;

        /**
         * @brief Writes a warning log line.
         *
         * @param message Message text.
         */
        static auto warning(const std::string& message) -> void;

        /**
         * @brief Writes an error log line.
         *
         * @param message Message text.
         */
        static auto error(const std::string& message) -> void;

        /**
         * @brief Writes a success log line.
         *
         * @param message Message text.
         */
        static auto success(const std::string& message) -> void;
    };

} // namespace vaultium