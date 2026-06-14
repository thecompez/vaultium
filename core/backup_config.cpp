module;

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

module vaultium_core_backup_config;

import vaultium_core_service_catalog;

namespace vaultium {
namespace {

[[nodiscard]] auto trim(std::string value) -> std::string
{
    const auto first = value.find_first_not_of(" \t\r\n");

    if (first == std::string::npos) {
        return {};
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

[[nodiscard]] auto lower(std::string value) -> std::string
{
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    return value;
}

[[nodiscard]] auto splitString(std::string value, char delimiter) -> std::vector<std::string>
{
    std::vector<std::string> result;
    std::stringstream stream { std::move(value) };
    std::string item;

    while (std::getline(stream, item, delimiter)) {
        item = trim(item);

        if (!item.empty()) {
            result.push_back(item);
        }
    }

    return result;
}

[[nodiscard]] auto splitPaths(std::string value, char delimiter) -> std::vector<std::filesystem::path>
{
    std::vector<std::filesystem::path> result;

    for (const auto& item : splitString(std::move(value), delimiter)) {
        result.emplace_back(item);
    }

    return result;
}

[[nodiscard]] auto pathExists(const std::filesystem::path& path) -> bool
{
    std::error_code error;
    return std::filesystem::exists(path, error) && !error;
}

[[nodiscard]] auto isSafeDatabaseName(const std::string& database) -> bool
{
    if (database.empty()) {
        return false;
    }

    return std::ranges::all_of(database, [](unsigned char character) {
        return std::isalnum(character) || character == '_' || character == '-';
    });
}

[[nodiscard]] auto parseEngineType(std::string value) -> BackupEngineType
{
    value = lower(trim(std::move(value)));

    if (value == "mysql" || value == "mariadb") {
        return BackupEngineType::MySql;
    }

    if (value == "postgresql" || value == "postgres" || value == "psql") {
        return BackupEngineType::PostgreSql;
    }

    if (value == "sqlite" || value == "sqlite3") {
        return BackupEngineType::Sqlite;
    }

    throw std::runtime_error("Unsupported BACKUP_ENGINE: " + value);
}

[[nodiscard]] auto parseSourceType(std::string value) -> SourceType
{
    value = lower(trim(std::move(value)));

    if (value == "database" || value == "db") {
        return SourceType::Database;
    }

    if (value == "filesystem" || value == "files" || value == "path" || value == "paths") {
        return SourceType::Filesystem;
    }

    if (value == "service-config" || value == "service_config" || value == "service") {
        return SourceType::ServiceConfig;
    }

    throw std::runtime_error("Unsupported BACKUP_SOURCE: " + value);
}

[[nodiscard]] auto parseRemoteAuthMethod(std::string value) -> RemoteAuthMethod
{
    value = lower(trim(std::move(value)));

    if (value == "key" || value == "private_key" || value == "publickey") {
        return RemoteAuthMethod::Key;
    }

    if (value == "password" || value == "pass") {
        return RemoteAuthMethod::Password;
    }

    throw std::runtime_error("Unsupported REMOTE_AUTH_METHOD: " + value);
}

[[nodiscard]] auto parseExecutionMode(std::string value) -> ExecutionMode
{
    value = lower(trim(std::move(value)));

    if (value == "local") {
        return ExecutionMode::Local;
    }

    if (value == "remote" || value == "ssh" || value == "remote_ssh") {
        return ExecutionMode::RemoteSsh;
    }

    if (value == "agent") {
        return ExecutionMode::Agent;
    }

    throw std::runtime_error("Unsupported EXECUTION_MODE: " + value);
}

} // namespace

auto parseBool(std::string value) -> bool
{
    value = lower(trim(std::move(value)));

    if (value == "true" || value == "yes" || value == "1" || value == "on") {
        return true;
    }

    if (value == "false" || value == "no" || value == "0" || value == "off") {
        return false;
    }

    throw std::runtime_error("Invalid boolean value: " + value);
}

auto loadBackupConfig(const std::filesystem::path& path, bool validate) -> BackupConfig
{
    BackupConfig config;

    std::ifstream file { path };

    if (!file) {
        throw std::runtime_error("Could not open config file: " + path.string());
    }

    std::string line;

    while (std::getline(file, line)) {
        line = trim(line);

        if (line.empty() || line.starts_with('#')) {
            continue;
        }

        const auto equalPosition = line.find('=');

        if (equalPosition == std::string::npos) {
            continue;
        }

        const auto key = trim(line.substr(0, equalPosition));
        const auto value = trim(line.substr(equalPosition + 1));

        if (key == "BACKUP_ENABLED") {
            config.enabled = parseBool(value);
        } else if (key == "BACKUP_SOURCE") {
            config.sourceType = parseSourceType(value);
        } else if (key == "BACKUP_ENGINE") {
            config.engineType = parseEngineType(value);
        } else if (key == "EXECUTION_MODE") {
            config.executionMode = parseExecutionMode(value);
        } else if (key == "BACKUP_COMPRESS") {
            config.compress = parseBool(value);
        } else if (key == "BACKUP_VALIDATE_GZIP") {
            config.validateGzip = parseBool(value);
        } else if (key == "BACKUP_CHECKSUM") {
            config.checksumEnabled = parseBool(value);
        } else if (key == "BACKUP_CLEANUP_ENABLED") {
            config.cleanupEnabled = parseBool(value);
        } else if (key == "BACKUP_PATHS") {
            config.backupPaths = splitPaths(value, ',');
        } else if (key == "BACKUP_SERVICES") {
            config.services = splitString(value, ',');
        } else if (key == "BACKUP_SERVICE_EXTRA_PATHS") {
            config.serviceExtraPaths = splitPaths(value, ',');
        } else if (key == "BACKUP_SERVICE_ROOT_PREFIX") {
            config.serviceRootPrefix = value;
        } else if (key == "TAR_PATH") {
            config.tarPath = value;
        } else if (key == "BACKUP_DATABASES") {
            const auto normalized = lower(value);

            if (normalized == "all") {
                config.databaseMode = DatabaseMode::All;
                config.databases.clear();
            } else {
                config.databaseMode = DatabaseMode::Selected;
                config.databases = splitString(value, ',');
            }
        } else if (key == "SQLITE_FILES") {
            config.sqliteFiles = splitPaths(value, ',');
        } else if (key == "BACKUP_DIR") {
            config.backupDirectory = value;
        } else if (key == "LOCK_FILE") {
            config.lockFile = value;
        } else if (key == "BACKUP_RETENTION_DAYS") {
            config.retentionDays = static_cast<std::size_t>(std::stoull(value));
        } else if (key == "BACKUP_INTERVAL_MINUTES") {
            config.interval = std::chrono::minutes { std::stoll(value) };
        } else if (key == "GZIP_PATH") {
            config.gzipPath = value;
        } else if (key == "MYSQL_DEFAULTS_FILE") {
            config.mysqlDefaultsFile = value;
        } else if (key == "MYSQLDUMP_PATH") {
            config.mysqldumpPath = value;
        } else if (key == "MYSQL_PATH") {
            config.mysqlClientPath = value;
        } else if (key == "MYSQL_HOST") {
            config.mysqlHost = value;
        } else if (key == "MYSQL_PORT") {
            config.mysqlPort = value;
        } else if (key == "MYSQL_USER") {
            config.mysqlUser = value;
        } else if (key == "MYSQL_PASSWORD") {
            config.mysqlPassword = value;
        } else if (key == "POSTGRES_HOST") {
            config.postgresHost = value;
        } else if (key == "POSTGRES_PORT") {
            config.postgresPort = value;
        } else if (key == "POSTGRES_USER") {
            config.postgresUser = value;
        } else if (key == "POSTGRES_PASSWORD_FILE") {
            config.postgresPasswordFile = value;
        } else if (key == "PG_DUMP_PATH") {
            config.pgDumpPath = value;
        } else if (key == "PG_DUMPALL_PATH") {
            config.pgDumpAllPath = value;
        } else if (key == "PSQL_PATH") {
            config.psqlPath = value;
        } else if (key == "REMOTE_HOST") {
            config.remoteHost = value;
        } else if (key == "REMOTE_USER") {
            config.remoteUser = value;
        } else if (key == "REMOTE_PORT") {
            config.remotePort = static_cast<std::uint16_t>(std::stoul(value));
        } else if (key == "REMOTE_IDENTITY_FILE") {
            config.remoteIdentityFile = value;
        } else if (key == "REMOTE_IDENTITY_PASSPHRASE") {
            config.remoteIdentityPassphrase = value;
        } else if (key == "REMOTE_KNOWN_HOSTS_FILE") {
            config.knownHostsFile = value;
        } else if (key == "REMOTE_STRICT_HOST_KEY") {
            config.strictHostKey = parseBool(value);
        } else if (key == "REMOTE_DOWNLOAD_DIR") {
            config.remoteDownloadDirectory = value;
        } else if (key == "REMOTE_SERVER_BACKUP_DIR") {
            config.remoteServerBackupDirectory = value;
        } else if (key == "REMOTE_REMOVE_AFTER_DOWNLOAD") {
            config.remoteRemoveAfterDownload = parseBool(value);
        } else if (key == "REMOTE_PROVISION_CONFIG_DIR") {
            config.remoteProvisionConfigDirectory = value;
        } else if (key == "REMOTE_PROVISION_ENABLED") {
            config.remoteProvisionEnabled = parseBool(value);
        } else if (key == "REMOTE_CONNECT_TIMEOUT_SECONDS") {
            config.remoteConnectTimeout = std::chrono::seconds { std::stoll(value) };
        } else if (key == "REMOTE_COMMAND_TIMEOUT_SECONDS") {
            config.remoteCommandTimeout = std::chrono::seconds { std::stoll(value) };
        } else if (key == "REMOTE_AUTH_METHOD") {
            config.remoteAuthMethod = parseRemoteAuthMethod(value);
        } else if (key == "REMOTE_PASSWORD") {
            config.remotePassword = value;
        } else {
            throw std::runtime_error("Unknown config key: " + key);
        }
    }

    if (validate) {
        validateBackupConfig(config);
    }

    return config;
}

auto validateBackupConfig(const BackupConfig& config) -> void
{
    if (config.interval.count() <= 0) {
        throw std::runtime_error("BACKUP_INTERVAL_MINUTES must be greater than zero.");
    }

    if (config.backupDirectory.empty()) {
        throw std::runtime_error("BACKUP_DIR cannot be empty.");
    }

    if (config.executionMode == ExecutionMode::Local && config.compress && !pathExists(config.gzipPath)) {
        throw std::runtime_error("gzip not found: " + config.gzipPath);
    }

    if (config.sourceType == SourceType::Filesystem) {
        if (config.backupPaths.empty()) {
            throw std::runtime_error("BACKUP_PATHS cannot be empty for filesystem backup.");
        }

        if (config.executionMode == ExecutionMode::Local) {
            if (!pathExists(config.tarPath)) {
                throw std::runtime_error("tar not found: " + config.tarPath);
            }

            for (const auto& backupPath : config.backupPaths) {
                if (!pathExists(backupPath)) {
                    throw std::runtime_error("Backup path not found: " + backupPath.string());
                }
            }
        }
    }

    if (config.sourceType == SourceType::ServiceConfig) {
        if (config.services.empty()) {
            throw std::runtime_error("BACKUP_SERVICES cannot be empty for service-config backup.");
        }

        for (const auto& service : config.services) {
            if (!isKnownService(service)) {
                throw std::runtime_error("Unknown service in BACKUP_SERVICES: " + service);
            }
        }

        if (config.executionMode == ExecutionMode::Local && !pathExists(config.tarPath)) {
            throw std::runtime_error("tar not found: " + config.tarPath);
        }
    }

    // Database-source-specific validation below.
    if (config.sourceType == SourceType::Database) {
    if (config.engineType == BackupEngineType::MySql) {
        if (config.executionMode == ExecutionMode::Local) {
            if (!pathExists(config.mysqldumpPath)) {
                throw std::runtime_error("mysqldump not found: " + config.mysqldumpPath);
            }

            if (!pathExists(config.mysqlDefaultsFile)) {
                throw std::runtime_error("MySQL defaults file not found: " + config.mysqlDefaultsFile.string());
            }
        }

        if (config.executionMode == ExecutionMode::RemoteSsh && config.remoteProvisionEnabled) {
            if (config.mysqlUser.empty()) {
                throw std::runtime_error("MYSQL_USER is required when REMOTE_PROVISION_ENABLED=true.");
            }

            if (config.mysqlPassword.empty()) {
                throw std::runtime_error("MYSQL_PASSWORD is required when REMOTE_PROVISION_ENABLED=true.");
            }

            if (config.mysqlHost.empty()) {
                throw std::runtime_error("MYSQL_HOST is required when REMOTE_PROVISION_ENABLED=true.");
            }

            if (config.mysqlPort.empty()) {
                throw std::runtime_error("MYSQL_PORT is required when REMOTE_PROVISION_ENABLED=true.");
            }
        }
    }

    if (config.engineType == BackupEngineType::PostgreSql && config.executionMode == ExecutionMode::Local) {
        if (config.databaseMode == DatabaseMode::All) {
            if (!pathExists(config.pgDumpAllPath)) {
                throw std::runtime_error("pg_dumpall not found: " + config.pgDumpAllPath);
            }
        } else if (!pathExists(config.pgDumpPath)) {
            throw std::runtime_error("pg_dump not found: " + config.pgDumpPath);
        }

        if (!pathExists(config.postgresPasswordFile)) {
            throw std::runtime_error("PostgreSQL password file not found: " + config.postgresPasswordFile.string());
        }
    }

    if (config.engineType == BackupEngineType::Sqlite && config.executionMode == ExecutionMode::Local) {
        if (config.sqliteFiles.empty()) {
            throw std::runtime_error("SQLITE_FILES cannot be empty for SQLite backup.");
        }

        for (const auto& sqliteFile : config.sqliteFiles) {
            if (!pathExists(sqliteFile)) {
                throw std::runtime_error("SQLite file not found: " + sqliteFile.string());
            }
        }
    }

    if (config.databaseMode == DatabaseMode::Selected && config.engineType != BackupEngineType::Sqlite) {
        if (config.databases.empty()) {
            throw std::runtime_error("BACKUP_DATABASES selected mode requires at least one database.");
        }

        for (const auto& database : config.databases) {
            if (!isSafeDatabaseName(database)) {
                throw std::runtime_error("Unsafe database name: " + database);
            }
        }
    }
    } // end SourceType::Database

    if (config.executionMode == ExecutionMode::RemoteSsh) {
        if (config.sourceType == SourceType::ServiceConfig && config.services.empty()) {
            throw std::runtime_error("BACKUP_SERVICES is required for remote service-config backup.");
        }

        if (config.sourceType == SourceType::Filesystem && config.backupPaths.empty()) {
            throw std::runtime_error("BACKUP_PATHS is required for remote filesystem backup.");
        }

        if (config.remoteHost.empty()) {
            throw std::runtime_error("REMOTE_HOST is required for remote SSH execution.");
        }

        if (config.remoteUser.empty()) {
            throw std::runtime_error("REMOTE_USER is required for remote SSH execution.");
        }

        if (config.remoteAuthMethod == RemoteAuthMethod::Key) {
            if (config.remoteIdentityFile.empty()) {
                throw std::runtime_error("REMOTE_IDENTITY_FILE is required for key authentication.");
            }

            if (!pathExists(config.remoteIdentityFile)) {
                throw std::runtime_error("SSH identity file not found: " + config.remoteIdentityFile.string());
            }
        }

        if (config.remoteAuthMethod == RemoteAuthMethod::Password) {
            if (config.remotePassword.empty()) {
                throw std::runtime_error("REMOTE_PASSWORD is required for password authentication.");
            }
        }

        if (config.remoteDownloadDirectory.empty()) {
            throw std::runtime_error("REMOTE_DOWNLOAD_DIR is required for remote SSH execution.");
        }

        if (config.remoteServerBackupDirectory.empty()) {
            throw std::runtime_error("REMOTE_SERVER_BACKUP_DIR is required for remote SSH execution.");
        }

        if (config.remoteConnectTimeout.count() <= 0) {
            throw std::runtime_error("REMOTE_CONNECT_TIMEOUT_SECONDS must be greater than zero.");
        }

        if (config.remoteCommandTimeout.count() <= 0) {
            throw std::runtime_error("REMOTE_COMMAND_TIMEOUT_SECONDS must be greater than zero.");
        }
    }
}

} // namespace vaultium