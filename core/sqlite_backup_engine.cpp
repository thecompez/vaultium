module;

#include <chrono>
#include <ctime>
#include <filesystem>
#include <vector>
#include <iostream>
#include <string>

module vaultium_core_sqlite_backup_engine;

import vaultium_core_process_runner;

namespace vaultium {

auto SqliteBackupEngine::name() const -> std::string
{
    return "sqlite";
}

auto SqliteBackupEngine::createBackup(
    const BackupConfig& config,
    const std::filesystem::path& temporaryFile
) const -> BackupArtifact
{
    const auto temporaryDirectory = std::filesystem::path { temporaryFile.string() + ".dir" };

    std::filesystem::remove_all(temporaryDirectory);
    std::filesystem::create_directories(temporaryDirectory);

    copySqliteFiles(config, temporaryDirectory);

    const std::string tarPath { "/usr/bin/tar" };

    if (!std::filesystem::exists(tarPath)) {
        std::filesystem::remove_all(temporaryDirectory);
        throw std::runtime_error("tar not found: " + tarPath);
    }

    const auto args = config.compress
        ? std::vector<std::string> {
            "-czf",
            temporaryFile.string(),
            "-C",
            temporaryDirectory.string(),
            "."
        }
        : std::vector<std::string> {
            "-cf",
            temporaryFile.string(),
            "-C",
            temporaryDirectory.string(),
            "."
        };

    const auto result = runProcessToFile(
        tarPath,
        args,
        "/dev/null"
    );

    std::filesystem::remove_all(temporaryDirectory);

    if (result.exitCode != 0) {
        throw std::runtime_error("SQLite archive failed with exit code: " + std::to_string(result.exitCode));
    }

    return BackupArtifact {
        .path = temporaryFile,
        .size = std::filesystem::file_size(temporaryFile)
    };
}

auto SqliteBackupEngine::copySqliteFiles(
    const BackupConfig& config,
    const std::filesystem::path& temporaryDirectory
) const -> void
{
    for (const auto& sqliteFile : config.sqliteFiles) {
        const auto target = temporaryDirectory / sqliteFile.filename();

        std::filesystem::copy_file(
            sqliteFile,
            target,
            std::filesystem::copy_options::overwrite_existing
        );

        std::filesystem::permissions(
            target,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace
        );
    }
}

} // namespace vaultium