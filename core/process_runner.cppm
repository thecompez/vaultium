module;

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

export module vaultium_core_process_runner;

import vaultium_core_types;

export namespace vaultium {

/**
 * @brief Environment variables applied only to the spawned child process.
 *
 * The parent process environment is never mutated. This is important for
 * credentials such as PostgreSQL passwords in multi-threaded clients.
 */
using ProcessEnvironment = std::vector<std::pair<std::string, std::string>>;

/**
 * @brief Runs a process and writes stdout to a file.
 * @param command Executable path.
 * @param args Process arguments.
 * @param outputFile Output file path.
 * @param environment Child-only environment variables.
 * @return Process result.
 */
[[nodiscard]] auto runProcessToFile(
    const std::string& command,
    const std::vector<std::string>& args,
    const std::filesystem::path& outputFile,
    const ProcessEnvironment& environment = {}
) -> ProcessResult;

/**
 * @brief Runs a dump process and pipes stdout through gzip.
 * @param dumpCommand Dump executable.
 * @param dumpArgs Dump arguments.
 * @param gzipCommand gzip executable.
 * @param outputFile Output file.
 * @param dumpEnvironment Child-only environment for the dump process.
 * @return Process result.
 */
[[nodiscard]] auto runDumpThroughGzip(
    const std::string& dumpCommand,
    const std::vector<std::string>& dumpArgs,
    const std::string& gzipCommand,
    const std::filesystem::path& outputFile,
    const ProcessEnvironment& dumpEnvironment = {}
) -> ProcessResult;

/**
 * @brief Validates a gzip archive.
 * @param gzipCommand gzip executable.
 * @param file File to validate.
 * @return true when valid.
 */
[[nodiscard]] auto validateGzipFile(
    const std::string& gzipCommand,
    const std::filesystem::path& file
) -> bool;

/**
 * @brief Runs a process with stdin connected to a file.
 * @param command Executable path.
 * @param args Process arguments.
 * @param inputFile File providing stdin.
 * @param environment Child-only environment variables.
 * @return Process result.
 */
[[nodiscard]] auto runProcessWithStdin(
    const std::string& command,
    const std::vector<std::string>& args,
    const std::filesystem::path& inputFile,
    const ProcessEnvironment& environment = {}
) -> ProcessResult;

/**
 * @brief Pipes `gzip -dc inputFile` into a process's stdin.
 * @param gzipCommand gzip executable.
 * @param command Executable path.
 * @param args Process arguments.
 * @param inputFile Compressed input file.
 * @param commandEnvironment Child-only environment for the target process.
 * @return Process result.
 */
[[nodiscard]] auto runGunzipIntoProcess(
    const std::string& gzipCommand,
    const std::string& command,
    const std::vector<std::string>& args,
    const std::filesystem::path& inputFile,
    const ProcessEnvironment& commandEnvironment = {}
) -> ProcessResult;

} // namespace vaultium
