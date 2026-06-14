module;

#include <chrono>
#include <ctime>
#include <filesystem>
#include <vector>
#include <iostream>
#include <string>

#if defined(VAULTIUM_PLATFORM_LINUX) || defined(VAULTIUM_PLATFORM_MACOS)
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

module vaultium_core_process_runner;

namespace vaultium {
namespace {

#if defined(VAULTIUM_PLATFORM_LINUX) || defined(VAULTIUM_PLATFORM_MACOS)

[[nodiscard]] auto makeArgv(
    const std::string& command,
    const std::vector<std::string>& args
) -> std::vector<char*>
{
    std::vector<char*> argv;
    argv.reserve(args.size() + 2);

    argv.push_back(const_cast<char*>(command.c_str()));

    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }

    argv.push_back(nullptr);

    return argv;
}

[[nodiscard]] auto waitForProcess(pid_t pid) -> int
{
    int status {};

    if (::waitpid(pid, &status, 0) == -1) {
        return 255;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return 255;
}

#endif

} // namespace

auto runProcessToFile(
    const std::string& command,
    const std::vector<std::string>& args,
    const std::filesystem::path& outputFile
) -> ProcessResult
{
#if defined(VAULTIUM_PLATFORM_LINUX) || defined(VAULTIUM_PLATFORM_MACOS)
    const auto outputFd = ::open(outputFile.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);

    if (outputFd == -1) {
        throw std::runtime_error("Could not open output file: " + outputFile.string());
    }

    const auto pid = ::fork();

    if (pid == -1) {
        ::close(outputFd);
        throw std::runtime_error("fork failed.");
    }

    if (pid == 0) {
        ::dup2(outputFd, STDOUT_FILENO);
        ::close(outputFd);

        auto argv = makeArgv(command, args);
        ::execv(command.c_str(), argv.data());

        std::exit(127);
    }

    ::close(outputFd);

    return ProcessResult {
        .exitCode = waitForProcess(pid)
    };
#else
    static_cast<void>(command);
    static_cast<void>(args);
    static_cast<void>(outputFile);

    throw std::runtime_error("Process execution is not implemented for this platform yet.");
#endif
}

auto runDumpThroughGzip(
    const std::string& dumpCommand,
    const std::vector<std::string>& dumpArgs,
    const std::string& gzipCommand,
    const std::filesystem::path& outputFile
) -> ProcessResult
{
#if defined(VAULTIUM_PLATFORM_LINUX) || defined(VAULTIUM_PLATFORM_MACOS)
    int pipeFds[2] {};

    if (::pipe(pipeFds) == -1) {
        throw std::runtime_error("pipe failed.");
    }

    const auto outputFd = ::open(outputFile.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);

    if (outputFd == -1) {
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        throw std::runtime_error("Could not open output file: " + outputFile.string());
    }

    const auto dumpPid = ::fork();

    if (dumpPid == -1) {
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        ::close(outputFd);
        throw std::runtime_error("fork failed for dump process.");
    }

    if (dumpPid == 0) {
        ::dup2(pipeFds[1], STDOUT_FILENO);

        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        ::close(outputFd);

        auto argv = makeArgv(dumpCommand, dumpArgs);
        ::execv(dumpCommand.c_str(), argv.data());

        std::exit(127);
    }

    const auto gzipPid = ::fork();

    if (gzipPid == -1) {
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        ::close(outputFd);
        throw std::runtime_error("fork failed for gzip process.");
    }

    if (gzipPid == 0) {
        ::dup2(pipeFds[0], STDIN_FILENO);
        ::dup2(outputFd, STDOUT_FILENO);

        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        ::close(outputFd);

        const std::vector<std::string> gzipArgs { "-c" };

        auto argv = makeArgv(gzipCommand, gzipArgs);
        ::execv(gzipCommand.c_str(), argv.data());

        std::exit(127);
    }

    ::close(pipeFds[0]);
    ::close(pipeFds[1]);
    ::close(outputFd);

    const auto dumpExitCode = waitForProcess(dumpPid);
    const auto gzipExitCode = waitForProcess(gzipPid);

    if (dumpExitCode != 0) {
        return ProcessResult {
            .exitCode = dumpExitCode
        };
    }

    return ProcessResult {
        .exitCode = gzipExitCode
    };
#else
    static_cast<void>(dumpCommand);
    static_cast<void>(dumpArgs);
    static_cast<void>(gzipCommand);
    static_cast<void>(outputFile);

    throw std::runtime_error("Process execution is not implemented for this platform yet.");
#endif
}

auto runProcessWithStdin(
    const std::string& command,
    const std::vector<std::string>& args,
    const std::filesystem::path& inputFile
) -> ProcessResult
{
#if defined(VAULTIUM_PLATFORM_LINUX) || defined(VAULTIUM_PLATFORM_MACOS)
    const auto inputFd = ::open(inputFile.c_str(), O_RDONLY);

    if (inputFd == -1) {
        throw std::runtime_error("Could not open input file: " + inputFile.string());
    }

    const auto pid = ::fork();

    if (pid == -1) {
        ::close(inputFd);
        throw std::runtime_error("fork failed.");
    }

    if (pid == 0) {
        ::dup2(inputFd, STDIN_FILENO);
        ::close(inputFd);

        auto argv = makeArgv(command, args);
        ::execv(command.c_str(), argv.data());

        std::exit(127);
    }

    ::close(inputFd);

    return ProcessResult {
        .exitCode = waitForProcess(pid)
    };
#else
    static_cast<void>(command);
    static_cast<void>(args);
    static_cast<void>(inputFile);

    throw std::runtime_error("Process execution is not implemented for this platform yet.");
#endif
}

auto runGunzipIntoProcess(
    const std::string& gzipCommand,
    const std::string& command,
    const std::vector<std::string>& args,
    const std::filesystem::path& inputFile
) -> ProcessResult
{
#if defined(VAULTIUM_PLATFORM_LINUX) || defined(VAULTIUM_PLATFORM_MACOS)
    int pipeFds[2] {};

    if (::pipe(pipeFds) == -1) {
        throw std::runtime_error("pipe failed.");
    }

    const auto gzipPid = ::fork();

    if (gzipPid == -1) {
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        throw std::runtime_error("fork failed for gunzip process.");
    }

    if (gzipPid == 0) {
        ::dup2(pipeFds[1], STDOUT_FILENO);

        ::close(pipeFds[0]);
        ::close(pipeFds[1]);

        const std::vector<std::string> gzipArgs { "-dc", inputFile.string() };

        auto argv = makeArgv(gzipCommand, gzipArgs);
        ::execv(gzipCommand.c_str(), argv.data());

        std::exit(127);
    }

    const auto commandPid = ::fork();

    if (commandPid == -1) {
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        throw std::runtime_error("fork failed for target process.");
    }

    if (commandPid == 0) {
        ::dup2(pipeFds[0], STDIN_FILENO);

        ::close(pipeFds[0]);
        ::close(pipeFds[1]);

        auto argv = makeArgv(command, args);
        ::execv(command.c_str(), argv.data());

        std::exit(127);
    }

    ::close(pipeFds[0]);
    ::close(pipeFds[1]);

    const auto gzipExitCode = waitForProcess(gzipPid);
    const auto commandExitCode = waitForProcess(commandPid);

    if (gzipExitCode != 0) {
        return ProcessResult {
            .exitCode = gzipExitCode
        };
    }

    return ProcessResult {
        .exitCode = commandExitCode
    };
#else
    static_cast<void>(gzipCommand);
    static_cast<void>(command);
    static_cast<void>(args);
    static_cast<void>(inputFile);

    throw std::runtime_error("Process execution is not implemented for this platform yet.");
#endif
}

auto validateGzipFile(
    const std::string& gzipCommand,
    const std::filesystem::path& file
) -> bool
{
#if defined(VAULTIUM_PLATFORM_LINUX) || defined(VAULTIUM_PLATFORM_MACOS)
    const auto pid = ::fork();

    if (pid == -1) {
        throw std::runtime_error("fork failed for gzip validation.");
    }

    if (pid == 0) {
        const std::vector<std::string> args {
            "-t",
            file.string()
        };

        auto argv = makeArgv(gzipCommand, args);
        ::execv(gzipCommand.c_str(), argv.data());

        std::exit(127);
    }

    return waitForProcess(pid) == 0;
#else
    static_cast<void>(gzipCommand);
    static_cast<void>(file);

    return false;
#endif
}

} // namespace vaultium