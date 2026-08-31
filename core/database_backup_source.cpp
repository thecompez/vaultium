module;

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

module vaultium_core_database_backup_source;

import vaultium_core_logger;
import vaultium_core_database_backup_engine;
import vaultium_core_mysql_backup_engine;
import vaultium_core_postgresql_backup_engine;
import vaultium_core_sqlite_backup_engine;
import vaultium_core_process_runner;

namespace vaultium {
namespace {

[[nodiscard]] auto createEngine(BackupEngineType type) -> std::unique_ptr<IDatabaseBackupEngine>
{
    switch (type) {
    case BackupEngineType::MySql:
        return std::make_unique<MySqlBackupEngine>();
    case BackupEngineType::PostgreSql:
        return std::make_unique<PostgreSqlBackupEngine>();
    case BackupEngineType::Sqlite:
        return std::make_unique<SqliteBackupEngine>();
    }
    throw std::runtime_error("Unsupported backup engine.");
}

[[nodiscard]] auto readFirstLine(const std::filesystem::path& path) -> std::string
{
    std::ifstream file { path };
    if (!file) {
        throw std::runtime_error("Could not read file: " + path.string());
    }

    std::string line;
    std::getline(file, line);
    if (line.empty()) {
        throw std::runtime_error("Credential file is empty: " + path.string());
    }
    return line;
}

[[nodiscard]] auto displayCommand(
    bool compressed,
    const std::string& gzipPath,
    const std::filesystem::path& archive,
    const std::string& client,
    const std::vector<std::string>& args
) -> std::string
{
    std::string rendered;
    if (compressed) {
        rendered += gzipPath + " -dc " + archive.string() + " | ";
    }
    rendered += client;
    for (const auto& arg : args) {
        rendered += " " + arg;
    }
    if (!compressed) {
        rendered += " < " + archive.string();
    }
    return rendered;
}

} // namespace

auto DatabaseBackupSource::type() const -> SourceType
{
    return SourceType::Database;
}

auto DatabaseBackupSource::name() const -> std::string
{
    return "database";
}

auto DatabaseBackupSource::artifactPrefix(const BackupConfig& config) const -> std::string
{
    switch (config.engineType) {
    case BackupEngineType::MySql:
        return config.databaseMode == DatabaseMode::All ? "mysql_full_" : "mysql_selected_";
    case BackupEngineType::PostgreSql:
        return config.databaseMode == DatabaseMode::All
            ? "postgresql_full_"
            : "postgresql_" + config.databases.front() + "_";
    case BackupEngineType::Sqlite:
        return "sqlite_files_";
    }
    return "database_";
}

auto DatabaseBackupSource::artifactExtension(const BackupConfig& config) const -> std::string
{
    if (config.engineType == BackupEngineType::Sqlite) {
        return config.compress ? ".tar.gz" : ".tar";
    }
    return config.compress ? ".sql.gz" : ".sql";
}

auto DatabaseBackupSource::createBackup(
    const BackupConfig& config,
    const std::filesystem::path& temporaryFile
) const -> BackupArtifact
{
    const auto engine = createEngine(config.engineType);
    return engine->createBackup(config, temporaryFile);
}

auto DatabaseBackupSource::restore(
    const BackupConfig& config,
    const std::filesystem::path& archive,
    const RestoreOptions& options
) const -> void
{
    if (!std::filesystem::exists(archive)) {
        throw std::runtime_error("Backup archive not found: " + archive.string());
    }

    const bool compressed = archive.string().ends_with(".gz");
    const bool execute = options.overwrite;

    const auto runClient = [&] (
        const std::string& client,
        const std::vector<std::string>& args,
        const ProcessEnvironment& environment = ProcessEnvironment {}
    ) {
        const auto result = compressed
            ? runGunzipIntoProcess(config.gzipPath, client, args, archive, environment)
            : runProcessWithStdin(client, args, archive, environment);
        if (result.exitCode != 0) {
            throw std::runtime_error("Database restore failed with exit code: " + std::to_string(result.exitCode));
        }
    };

    switch (config.engineType) {
    case BackupEngineType::MySql: {
        std::vector<std::string> args;
        if (!config.mysqlDefaultsFile.empty()) {
            args.push_back("--defaults-extra-file=" + config.mysqlDefaultsFile.string());
        }

        if (!execute) {
            Logger::info("Dry run (pass overwrite to apply). Would run:");
            Logger::info("  " + displayCommand(compressed, config.gzipPath, archive, config.mysqlClientPath, args));
            return;
        }

        Logger::warning("Destructive restore: importing dump into the live MySQL/MariaDB server.");
        runClient(config.mysqlClientPath, args);
        Logger::success("MySQL restore completed.");
        break;
    }

    case BackupEngineType::PostgreSql: {
        const auto targetDb = config.databaseMode == DatabaseMode::All
            ? std::string { "postgres" }
            : (config.databases.empty() ? std::string { "postgres" } : config.databases.front());

        const std::vector<std::string> args {
            "--host=" + config.postgresHost,
            "--port=" + config.postgresPort,
            "--username=" + config.postgresUser,
            "--no-password",
            "--dbname=" + targetDb
        };

        if (!execute) {
            Logger::info("Dry run (pass overwrite to apply). Would run:");
            Logger::info("  PGPASSWORD=*** " + displayCommand(compressed, config.gzipPath, archive, config.psqlPath, args));
            return;
        }

        Logger::warning("Destructive restore: importing dump into the live PostgreSQL server.");
        const ProcessEnvironment environment {
            { "PGPASSWORD", readFirstLine(config.postgresPasswordFile) }
        };
        runClient(config.psqlPath, args, environment);
        Logger::success("PostgreSQL restore completed.");
        break;
    }

    case BackupEngineType::Sqlite: {
        if (options.destination.empty()) {
            throw std::runtime_error("Restore destination is required for SQLite restore.");
        }

        const std::vector<std::string> args {
            compressed ? "-xzf" : "-xf",
            archive.string(),
            "-C",
            options.destination.string()
        };

        if (!execute) {
            Logger::info("Dry run (pass overwrite to apply). Would extract SQLite files:");
            Logger::info("  " + config.tarPath + " " + args[0] + " " + archive.string()
                + " -C " + options.destination.string());
            return;
        }

        Logger::warning("Destructive restore: extracting SQLite files to " + options.destination.string());
        std::filesystem::create_directories(options.destination);
        const auto result = runProcessToFile(config.tarPath, args, "/dev/null");
        if (result.exitCode != 0) {
            throw std::runtime_error("SQLite restore failed with exit code: " + std::to_string(result.exitCode));
        }
        Logger::success("SQLite restore completed into " + options.destination.string());
        break;
    }
    }
}

auto DatabaseBackupSource::verify(
    const BackupConfig& config,
    const std::filesystem::path& archive
) const -> bool
{
    if (!std::filesystem::exists(archive) || std::filesystem::file_size(archive) == 0) {
        return false;
    }
    if (archive.string().ends_with(".gz")) {
        return validateGzipFile(config.gzipPath, archive);
    }
    return true;
}

} // namespace vaultium
