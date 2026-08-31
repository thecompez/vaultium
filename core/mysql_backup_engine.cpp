module;

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

module vaultium_core_mysql_backup_engine;

import vaultium_core_process_runner;

namespace vaultium {

auto MySqlBackupEngine::name() const -> std::string
{
    return "mysql";
}

auto MySqlBackupEngine::createBackup(
    const BackupConfig& config,
    const std::filesystem::path& temporaryFile
) const -> BackupArtifact
{
    const auto args = buildArguments(config);
    ProcessResult result {};

    if (config.compress) {
        result = runDumpThroughGzip(
            config.mysqldumpPath,
            args,
            config.gzipPath,
            temporaryFile
        );
        if (result.exitCode == 0 && config.validateGzip
            && !validateGzipFile(config.gzipPath, temporaryFile)) {
            throw std::runtime_error("MySQL gzip validation failed.");
        }
    } else {
        result = runProcessToFile(config.mysqldumpPath, args, temporaryFile);
    }

    if (result.exitCode != 0) {
        throw std::runtime_error("mysqldump failed with exit code: " + std::to_string(result.exitCode));
    }

    return BackupArtifact {
        .path = temporaryFile,
        .size = std::filesystem::file_size(temporaryFile)
    };
}

auto MySqlBackupEngine::buildArguments(const BackupConfig& config) const -> std::vector<std::string>
{
    std::vector<std::string> args;
    if (!config.mysqlDefaultsFile.empty()) {
        args.push_back("--defaults-extra-file=" + config.mysqlDefaultsFile.string());
    }

    args.insert(args.end(), {
        "--single-transaction",
        "--routines",
        "--triggers",
        "--events",
        "--hex-blob",
        "--default-character-set=utf8mb4",
        "--set-gtid-purged=OFF"
    });

    if (config.databaseMode == DatabaseMode::All) {
        args.emplace_back("--all-databases");
    } else {
        args.emplace_back("--databases");
        args.insert(args.end(), config.databases.begin(), config.databases.end());
    }

    return args;
}

} // namespace vaultium
