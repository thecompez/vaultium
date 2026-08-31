module;

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

export module vaultium_core_backup_config;

import vaultium_core_types;

export namespace vaultium {

/**
 * @brief Runtime configuration for Vaultium backup jobs.
 */
struct BackupConfig {
    bool enabled { true };
    bool compress { true };
    bool validateGzip { true };
    bool checksumEnabled { true };
    bool cleanupEnabled { true };

    SourceType sourceType { SourceType::Database };
    BackupEngineType engineType { BackupEngineType::MySql };
    DatabaseMode databaseMode { DatabaseMode::All };
    ExecutionMode executionMode { ExecutionMode::Local };

    std::vector<std::string> databases {};
    std::vector<std::filesystem::path> sqliteFiles {};
    std::vector<std::filesystem::path> backupPaths {};
    std::vector<std::string> services {};
    std::vector<std::filesystem::path> serviceExtraPaths {};
    std::filesystem::path serviceRootPrefix {};

    std::filesystem::path backupDirectory { "/var/backups/vaultium" };
    std::filesystem::path lockFile { "/run/vaultium.lock" };

    std::size_t retentionDays { 7 };
    std::chrono::minutes interval { 1440 };

    std::string gzipPath { "/usr/bin/gzip" };
    std::string tarPath { "/usr/bin/tar" };
    std::string sqlite3Path { "/usr/bin/sqlite3" };

    std::filesystem::path mysqlDefaultsFile {};
    std::string mysqldumpPath { "/usr/bin/mysqldump" };
    std::string mysqlClientPath { "/usr/bin/mysql" };
    std::string mysqlHost { "127.0.0.1" };
    std::string mysqlPort { "3306" };
    std::string mysqlUser {};
    std::string mysqlPassword {};

    std::string postgresHost { "127.0.0.1" };
    std::string postgresPort { "5432" };
    std::string postgresUser { "backup_user" };
    std::filesystem::path postgresPasswordFile { "/etc/vaultium/postgres_password" };
    std::string pgDumpPath { "/usr/bin/pg_dump" };
    std::string pgDumpAllPath { "/usr/bin/pg_dumpall" };
    std::string psqlPath { "/usr/bin/psql" };

    std::string remoteHost {};
    std::string remoteUser {};
    std::uint16_t remotePort { 22 };
    RemoteAuthMethod remoteAuthMethod { RemoteAuthMethod::Key };
    std::filesystem::path remoteIdentityFile {};
    std::string remoteIdentityPassphrase {};
    std::string remotePassword {};
    std::filesystem::path knownHostsFile {};
    bool strictHostKey { true };
    std::filesystem::path remoteDownloadDirectory {};
    std::filesystem::path remoteServerBackupDirectory { "/tmp/vaultium_remote_backups" };
    bool remoteRemoveAfterDownload { true };
    std::filesystem::path remoteProvisionConfigDirectory { "/etc/vaultium" };
    bool remoteProvisionEnabled { false };
    std::chrono::seconds remoteConnectTimeout { 15 };
    std::chrono::seconds remoteCommandTimeout { 600 };
};

/**
 * @brief Loads backup configuration from a key-value file.
 * @param path Configuration file path.
 * @param validate Whether to validate the loaded configuration.
 * @return Parsed backup configuration.
 */
[[nodiscard]] auto loadBackupConfig(
    const std::filesystem::path& path,
    bool validate = true
) -> BackupConfig;

/**
 * @brief Validates backup configuration.
 * @param config Configuration object.
 */
auto validateBackupConfig(const BackupConfig& config) -> void;

/**
 * @brief Parses a boolean value.
 * @param value Input string.
 * @return Boolean value.
 */
[[nodiscard]] auto parseBool(std::string value) -> bool;

} // namespace vaultium
