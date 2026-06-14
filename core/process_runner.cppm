module;

#include <iostream>
#include <filesystem>
#include <string>

export module vaultium_core_process_runner;

import vaultium_core_types;

export namespace vaultium {

    /**
     * @brief Runs a process and writes stdout to a file.
     *
     * @param command Executable path.
     * @param args Process arguments.
     * @param outputFile Output file path.
     * @return Process result.
     */
    [[nodiscard]] auto runProcessToFile(
        const std::string& command,
        const std::vector<std::string>& args,
        const std::filesystem::path& outputFile
    ) -> ProcessResult;

    /**
     * @brief Runs a dump process and pipes stdout through gzip.
     *
     * @param dumpCommand Dump executable.
     * @param dumpArgs Dump arguments.
     * @param gzipCommand gzip executable.
     * @param outputFile Output file.
     * @return Process result.
     */
    [[nodiscard]] auto runDumpThroughGzip(
        const std::string& dumpCommand,
        const std::vector<std::string>& dumpArgs,
        const std::string& gzipCommand,
        const std::filesystem::path& outputFile
    ) -> ProcessResult;

    /**
     * @brief Validates gzip archive.
     *
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
     *
     * stdout/stderr are inherited from the parent so client output is visible.
     * Used to feed an uncompressed dump into a database client (e.g. mysql/psql).
     *
     * @param command Executable path.
     * @param args Process arguments.
     * @param inputFile File providing stdin.
     * @return Process result.
     */
    [[nodiscard]] auto runProcessWithStdin(
        const std::string& command,
        const std::vector<std::string>& args,
        const std::filesystem::path& inputFile
    ) -> ProcessResult;

    /**
     * @brief Pipes `gzip -dc inputFile` into a process's stdin.
     *
     * Equivalent to `gzip -dc inputFile | command args` without a shell.
     *
     * @param gzipCommand gzip executable.
     * @param command Executable path.
     * @param args Process arguments.
     * @param inputFile Compressed input file.
     * @return Process result (the command's exit code, or gzip's on gzip failure).
     */
    [[nodiscard]] auto runGunzipIntoProcess(
        const std::string& gzipCommand,
        const std::string& command,
        const std::vector<std::string>& args,
        const std::filesystem::path& inputFile
    ) -> ProcessResult;

} // namespace vaultium