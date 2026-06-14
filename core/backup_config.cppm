module;

#include <chrono>
#include <ctime>
#include <filesystem>
#include <vector>
#include <iostream>
#include <string>

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

        // Filesystem source: arbitrary files/directories to archive.
        std::vector<std::filesystem::path> backupPaths {};

        // Service-config source: curated services to back up (e.g. nginx,systemd)
        // plus any explicitly configured extra paths (e.g. docker compose files).
        std::vector<std::string> services {};
        std::vector<std::filesystem::path> serviceExtraPaths {};

        // Optional sandbox root for service-config: when set, curated absolute
        // paths are relocated under this prefix (e.g. for a mounted volume,
        // chroot, or test fixture). Empty means the real filesystem root.
        std::filesystem::path serviceRootPrefix {};

        std::filesystem::path backupDirectory { "/var/backups/vaultium" };
        std::filesystem::path lockFile { "/run/vaultium.lock" };

        std::size_t retentionDays { 7 };
        std::chrono::minutes interval { 1440 };

        std::string gzipPath { "/usr/bin/gzip" };
        std::string tarPath { "/usr/bin/tar" };

        // Empty by default: when unset, the engine uses the server's default
        // auth (e.g. socket auth) and auto-detects mariadb-dump/mysqldump.
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

        // Host-key verification. When empty, knownHostsFile defaults to
        // ~/.ssh/known_hosts at connect time. Strict mode refuses unknown or
        // mismatched host keys.
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
 *
 * @param path Configuration file path.
 * @return Parsed backup configuration.
 */
[[nodiscard]] auto loadBackupConfig(const std::filesystem::path& path, bool validate = true) -> BackupConfig;

/**
 * @brief Validates backup configuration.
 *
 * @param config Configuration object.
 */
auto validateBackupConfig(const BackupConfig& config) -> void;

/**
 * @brief Parses a boolean value.
 *
 * @param value Input string.
 * @return Boolean value.
 */
[[nodiscard]] auto parseBool(std::string value) -> bool;

} // namespace vaultium