module;

#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

module vaultium_core_sqlite_backup_engine;

import vaultium_core_process_runner;

namespace vaultium {
namespace {

[[nodiscard]] auto sqliteQuote(const std::filesystem::path& path) -> std::string
{
    std::string value = path.string();
    std::string quoted { "'" };
    for (const char character : value) {
        if (character == '\'') {
            quoted += "''";
        } else {
            quoted.push_back(character);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

} // namespace

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

    try {
        snapshotSqliteFiles(config, temporaryDirectory);

        const auto args = config.compress
            ? std::vector<std::string> { "-czf", temporaryFile.string(), "-C", temporaryDirectory.string(), "." }
            : std::vector<std::string> { "-cf", temporaryFile.string(), "-C", temporaryDirectory.string(), "." };

        const auto result = runProcessToFile(config.tarPath, args, "/dev/null");
        if (result.exitCode != 0) {
            throw std::runtime_error("SQLite archive failed with exit code: " + std::to_string(result.exitCode));
        }

        if (config.compress && config.validateGzip
            && !validateGzipFile(config.gzipPath, temporaryFile)) {
            throw std::runtime_error("SQLite archive gzip validation failed.");
        }
    } catch (...) {
        std::filesystem::remove_all(temporaryDirectory);
        std::error_code error;
        std::filesystem::remove(temporaryFile, error);
        throw;
    }

    std::filesystem::remove_all(temporaryDirectory);

    return BackupArtifact {
        .path = temporaryFile,
        .size = std::filesystem::file_size(temporaryFile)
    };
}

auto SqliteBackupEngine::snapshotSqliteFiles(
    const BackupConfig& config,
    const std::filesystem::path& temporaryDirectory
) const -> void
{
    std::unordered_set<std::string> names;

    for (const auto& sqliteFile : config.sqliteFiles) {
        const auto fileName = sqliteFile.filename().string();
        if (!names.insert(fileName).second) {
            throw std::runtime_error(
                "SQLite backup contains duplicate filenames: " + fileName +
                ". Use unique database filenames to avoid ambiguous restores."
            );
        }

        const auto target = temporaryDirectory / sqliteFile.filename();
        const std::vector<std::string> args {
            sqliteFile.string(),
            ".backup " + sqliteQuote(target)
        };

        const auto result = runProcessToFile(config.sqlite3Path, args, "/dev/null");
        if (result.exitCode != 0 || !std::filesystem::exists(target)
            || std::filesystem::file_size(target) == 0) {
            throw std::runtime_error("SQLite online backup failed for: " + sqliteFile.string());
        }

        std::filesystem::permissions(
            target,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace
        );
    }
}

} // namespace vaultium
