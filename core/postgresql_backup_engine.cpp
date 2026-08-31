module;

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

module vaultium_core_postgresql_backup_engine;

import vaultium_core_process_runner;

namespace vaultium {

auto PostgreSqlBackupEngine::name() const -> std::string
{
    return "postgresql";
}

auto PostgreSqlBackupEngine::createBackup(
    const BackupConfig& config,
    const std::filesystem::path& temporaryFile
) const -> BackupArtifact
{
#if defined(VAULTIUM_PLATFORM_LINUX) || defined(VAULTIUM_PLATFORM_MACOS)
    const ProcessEnvironment environment {
        { "PGPASSWORD", readPassword(config.postgresPasswordFile) }
    };

    ProcessResult result {};

    if (config.databaseMode == DatabaseMode::All) {
        const auto args = buildPgDumpAllArguments(config);
        result = config.compress
            ? runDumpThroughGzip(config.pgDumpAllPath, args, config.gzipPath, temporaryFile, environment)
            : runProcessToFile(config.pgDumpAllPath, args, temporaryFile, environment);
    } else {
        if (config.databases.size() != 1) {
            throw std::runtime_error("PostgreSQL selected mode supports one database per backup job.");
        }

        const auto args = buildPgDumpArguments(config, config.databases.front());
        result = config.compress
            ? runDumpThroughGzip(config.pgDumpPath, args, config.gzipPath, temporaryFile, environment)
            : runProcessToFile(config.pgDumpPath, args, temporaryFile, environment);
    }

    if (result.exitCode != 0) {
        throw std::runtime_error("PostgreSQL dump failed with exit code: " + std::to_string(result.exitCode));
    }

    if (config.compress && config.validateGzip && !validateGzipFile(config.gzipPath, temporaryFile)) {
        throw std::runtime_error("PostgreSQL gzip validation failed.");
    }

    return BackupArtifact {
        .path = temporaryFile,
        .size = std::filesystem::file_size(temporaryFile)
    };
#else
    static_cast<void>(config);
    static_cast<void>(temporaryFile);
    throw std::runtime_error("PostgreSQL backup is not implemented for this platform yet.");
#endif
}

auto PostgreSqlBackupEngine::buildPgDumpArguments(
    const BackupConfig& config,
    const std::string& database
) const -> std::vector<std::string>
{
    return {
        "--host=" + config.postgresHost,
        "--port=" + config.postgresPort,
        "--username=" + config.postgresUser,
        "--format=plain",
        "--clean",
        "--if-exists",
        "--no-password",
        database
    };
}

auto PostgreSqlBackupEngine::buildPgDumpAllArguments(const BackupConfig& config) const -> std::vector<std::string>
{
    return {
        "--host=" + config.postgresHost,
        "--port=" + config.postgresPort,
        "--username=" + config.postgresUser,
        "--clean",
        "--if-exists",
        "--no-password"
    };
}

auto PostgreSqlBackupEngine::readPassword(const std::filesystem::path& path) const -> std::string
{
    std::ifstream file { path };
    if (!file) {
        throw std::runtime_error("Could not read PostgreSQL password file: " + path.string());
    }

    std::string password;
    std::getline(file, password);
    if (password.empty()) {
        throw std::runtime_error("PostgreSQL password file is empty.");
    }
    return password;
}

} // namespace vaultium
