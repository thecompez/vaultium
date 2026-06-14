module;

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

module vaultium_remote_client;

import vaultium_core_logger;
import vaultium_core_types;
import vaultium_core_backup_config;
import vaultium_core_backup_source;
import vaultium_core_service_catalog;
import vaultium_remote_ssh_client;
import vaultium_remote_provisioner;

namespace vaultium::remote {
namespace {

[[nodiscard]] auto shellQuote(const std::string& value) -> std::string
{
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('\'');

    for (const auto character : value) {
        if (character == '\'') {
            result += "'\\''";
        } else {
            result.push_back(character);
        }
    }

    result.push_back('\'');
    return result;
}

[[nodiscard]] auto timestamp() -> std::string
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime {};

#if defined(VAULTIUM_PLATFORM_WINDOWS)
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y-%m-%d_%H-%M-%S");

    return stream.str();
}

[[nodiscard]] auto joinCommandParts(const std::vector<std::string>& parts) -> std::string
{
    std::ostringstream stream;

    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index > 0) {
            stream << ' ';
        }

        stream << parts[index];
    }

    return stream.str();
}

[[nodiscard]] auto buildMysqlRemoteCommand(
    const BackupConfig& config,
    const std::string& remoteFile
) -> std::string
{
    // Use a shell-resolved dump tool ($VAULTIUM_DUMP), preferring mariadb-dump
    // (mysqldump is deprecated on MariaDB) and falling back to mysqldump.
    std::vector<std::string> dumpParts { "\"$VAULTIUM_DUMP\"" };

    // Only pass a defaults file when configured; otherwise let the dump tool use
    // the server's default auth (e.g. socket auth for root). An empty
    // --defaults-extra-file= makes the dump fail.
    if (!config.mysqlDefaultsFile.empty()) {
        dumpParts.push_back("--defaults-extra-file=" + shellQuote(config.mysqlDefaultsFile.string()));
    }

    dumpParts.insert(dumpParts.end(), {
        "--single-transaction",
        "--routines",
        "--triggers",
        "--events",
        "--hex-blob",
        "--default-character-set=utf8mb4",
        "$GTID" // expands to --set-gtid-purged=OFF only for mysqldump (not MariaDB)
    });

    if (config.databaseMode == DatabaseMode::All) {
        dumpParts.emplace_back("--all-databases");
    } else {
        dumpParts.emplace_back("--databases");

        for (const auto& database : config.databases) {
            dumpParts.push_back(shellQuote(database));
        }
    }

    // Resolve the dump tool on the server: prefer mariadb-dump (mysqldump is
    // deprecated on MariaDB), then the configured path, then mysqldump.
    const std::string detectDump =
        "VAULTIUM_DUMP=\"$(command -v mariadb-dump || command -v "
        + shellQuote(config.mysqldumpPath) + " || command -v mysqldump || echo mysqldump)\"; "
        // --set-gtid-purged is MySQL-only; MariaDB's mariadb-dump rejects it.
        "case \"$VAULTIUM_DUMP\" in *mariadb*) GTID=\"\";; *) GTID=\"--set-gtid-purged=OFF\";; esac; ";

    const auto dumpCommand = detectDump + joinCommandParts(dumpParts);

    std::ostringstream command;

    command
        << "set -e; "
        // Enable pipefail when the remote shell supports it (bash), so a failing
        // dump in `dump | gzip` fails the command instead of producing a tiny file.
        << "(set -o pipefail) 2>/dev/null && set -o pipefail || true; "
        << "mkdir -p " << shellQuote(config.remoteServerBackupDirectory.string()) << "; "
        << "chmod 700 " << shellQuote(config.remoteServerBackupDirectory.string()) << "; ";

    if (config.compress) {
        command
            << dumpCommand
            << " | "
            << shellQuote(config.gzipPath)
            << " -c > "
            << shellQuote(remoteFile)
            << "; "
            << shellQuote(config.gzipPath)
            << " -t "
            << shellQuote(remoteFile)
            << "; ";
    } else {
        command
            << dumpCommand
            << " > "
            << shellQuote(remoteFile)
            << "; ";
    }

    command
        << "test -s "
        << shellQuote(remoteFile)
        << "; chmod 600 "
        << shellQuote(remoteFile);

    return command.str();
}

[[nodiscard]] auto buildPostgreSqlRemoteCommand(
    const BackupConfig& config,
    const std::string& remoteFile
) -> std::string
{
    std::string dumpCommand;

    if (config.databaseMode == DatabaseMode::All) {
        dumpCommand =
            shellQuote(config.pgDumpAllPath) +
            " --host=" + shellQuote(config.postgresHost) +
            " --port=" + shellQuote(config.postgresPort) +
            " --username=" + shellQuote(config.postgresUser) +
            " --clean --if-exists --no-password";
    } else {
        if (config.databases.size() != 1) {
            throw std::runtime_error("Remote PostgreSQL selected mode supports one database per job.");
        }

        dumpCommand =
            shellQuote(config.pgDumpPath) +
            " --host=" + shellQuote(config.postgresHost) +
            " --port=" + shellQuote(config.postgresPort) +
            " --username=" + shellQuote(config.postgresUser) +
            " --format=plain --clean --if-exists --no-password " +
            shellQuote(config.databases.front());
    }

    std::ostringstream command;

    command
        << "set -e; "
        << "mkdir -p " << shellQuote(config.remoteServerBackupDirectory.string()) << "; "
        << "chmod 700 " << shellQuote(config.remoteServerBackupDirectory.string()) << "; "
        << "export PGPASSWORD=$(cat " << shellQuote(config.postgresPasswordFile.string()) << "); ";

    if (config.compress) {
        command
            << dumpCommand
            << " | "
            << shellQuote(config.gzipPath)
            << " -c > "
            << shellQuote(remoteFile)
            << "; "
            << shellQuote(config.gzipPath)
            << " -t "
            << shellQuote(remoteFile)
            << "; ";
    } else {
        command
            << dumpCommand
            << " > "
            << shellQuote(remoteFile)
            << "; ";
    }

    command
        << "test -s "
        << shellQuote(remoteFile)
        << "; chmod 600 "
        << shellQuote(remoteFile);

    return command.str();
}

[[nodiscard]] auto buildSqliteRemoteCommand(
    const BackupConfig& config,
    const std::string& remoteFile
) -> std::string
{
    const auto temporaryDirectory = config.remoteServerBackupDirectory.string() + "/sqlite_" + timestamp();

    std::ostringstream command;

    command
        << "set -e; "
        << "mkdir -p " << shellQuote(config.remoteServerBackupDirectory.string()) << "; "
        << "chmod 700 " << shellQuote(config.remoteServerBackupDirectory.string()) << "; "
        << "rm -rf " << shellQuote(temporaryDirectory) << "; "
        << "mkdir -p " << shellQuote(temporaryDirectory) << "; ";

    for (const auto& sqliteFile : config.sqliteFiles) {
        command
            << "cp "
            << shellQuote(sqliteFile.string())
            << " "
            << shellQuote(temporaryDirectory + "/")
            << "; ";
    }

    command
        << "/usr/bin/tar --ignore-failed-read "
        << (config.compress ? "-czf " : "-cf ")
        << shellQuote(remoteFile)
        << " -C "
        << shellQuote(temporaryDirectory)
        << " .; "
        << "rm -rf "
        << shellQuote(temporaryDirectory)
        << "; "
        << "test -s "
        << shellQuote(remoteFile)
        << "; chmod 600 "
        << shellQuote(remoteFile);

    return command.str();
}

[[nodiscard]] auto buildFilesystemRemoteCommand(
    const BackupConfig& config,
    const std::string& remoteFile
) -> std::string
{
    if (config.backupPaths.empty()) {
        throw std::runtime_error("BACKUP_PATHS cannot be empty for remote filesystem backup.");
    }

    auto root = config.backupPaths.front().root_path();

    if (root.empty()) {
        root = "/";
    }

    std::ostringstream command;

    command
        << "set -e; "
        << "mkdir -p " << shellQuote(config.remoteServerBackupDirectory.string()) << "; "
        << "chmod 700 " << shellQuote(config.remoteServerBackupDirectory.string()) << "; "
        << shellQuote(config.tarPath) << " --ignore-failed-read "
        << (config.compress ? "-czf " : "-cf ")
        << shellQuote(remoteFile)
        << " -C " << shellQuote(root.string());

    for (const auto& path : config.backupPaths) {
        if (!path.is_absolute()) {
            throw std::runtime_error("Remote filesystem backup paths must be absolute: " + path.string());
        }

        if (path.root_path() != root) {
            throw std::runtime_error("All BACKUP_PATHS must share the same filesystem root.");
        }

        command << ' ' << shellQuote(path.relative_path().generic_string());
    }

    command
        << "; test -s " << shellQuote(remoteFile)
        << "; chmod 600 " << shellQuote(remoteFile);

    return command.str();
}

[[nodiscard]] auto buildServiceConfigRemoteCommand(
    const BackupConfig& config,
    const std::string& remoteFile
) -> std::string
{
    if (config.services.empty()) {
        throw std::runtime_error("BACKUP_SERVICES cannot be empty for remote service-config backup.");
    }

    std::vector<std::string> candidates;
    for (const auto& service : config.services) {
        if (!isKnownService(service)) {
            continue;
        }
        for (const auto& path : serviceDefinition(service).candidatePaths) {
            candidates.push_back(path.string());
        }
    }
    if (candidates.empty()) {
        throw std::runtime_error("None of the selected services are known to Vaultium.");
    }

    std::ostringstream command;
    command
        << "set -e; "
        << "mkdir -p " << shellQuote(config.remoteServerBackupDirectory.string()) << "; "
        << "chmod 700 " << shellQuote(config.remoteServerBackupDirectory.string()) << "; "
        << "SEL=\"\"; for p in";
    for (const auto& candidate : candidates) {
        command << ' ' << shellQuote(candidate);
    }
    command
        << "; do [ -e \"$p\" ] && SEL=\"$SEL $p\"; done; "
        << "[ -n \"$SEL\" ] || { echo 'No service configuration paths exist on the server.' 1>&2; exit 1; }; "
        << shellQuote(config.tarPath) << " --ignore-failed-read "
        << (config.compress ? "-czf " : "-cf ")
        << shellQuote(remoteFile) << " -P $SEL; "
        << "test -s " << shellQuote(remoteFile) << "; chmod 600 " << shellQuote(remoteFile);
    return command.str();
}

[[nodiscard]] auto buildRemoteCommand(
    const BackupConfig& config,
    const std::string& remoteFile
) -> std::string
{
    if (config.sourceType == SourceType::Filesystem) {
        return buildFilesystemRemoteCommand(config, remoteFile);
    }

    if (config.sourceType == SourceType::ServiceConfig) {
        return buildServiceConfigRemoteCommand(config, remoteFile);
    }

    // Database source.
    switch (config.engineType) {
    case BackupEngineType::MySql:
        return buildMysqlRemoteCommand(config, remoteFile);

    case BackupEngineType::PostgreSql:
        return buildPostgreSqlRemoteCommand(config, remoteFile);

    case BackupEngineType::Sqlite:
        return buildSqliteRemoteCommand(config, remoteFile);
    }

    throw std::runtime_error("Unsupported remote backup engine.");
}

auto removeRemoteFile(
    SshClient& ssh,
    const BackupConfig& config,
    const std::string& remoteFile
) -> void
{
    if (!config.remoteRemoveAfterDownload) {
        return;
    }

    Logger::info("Removing remote temporary file.");

    const auto result = ssh.execute("rm -f " + shellQuote(remoteFile));

    if (result.exitCode != 0) {
        Logger::warning("Could not remove remote temporary file: " + result.errorOutput);
    }
}

} // namespace

auto runRemoteBackup(const BackupConfig& config) -> int
{
    try {
        Logger::info("Remote libssh2 backup mode selected.");
        Logger::info("Host: " + config.remoteHost);
        Logger::info("User: " + config.remoteUser);
        Logger::info("Port: " + std::to_string(config.remotePort));

        // Naming is driven by the same IBackupSource used for local backups, so
        // remote artifacts match local conventions for each source type.
        const auto source = createBackupSource(config);
        const auto fileName = source->artifactPrefix(config) + timestamp() + source->artifactExtension(config);
        const auto remoteFile = (config.remoteServerBackupDirectory / fileName).string();
        const auto localFile = config.remoteDownloadDirectory / fileName;

        const auto remoteCommand = buildRemoteCommand(config, remoteFile);

        SshConnectionConfig sshConfig {
            .host = config.remoteHost,
            .port = config.remotePort,
            .username = config.remoteUser,
            .authMethod = config.remoteAuthMethod,
            .privateKeyPath = config.remoteIdentityFile,
            .privateKeyPassphrase = config.remoteIdentityPassphrase,
            .password = config.remotePassword,
            .knownHostsFile = config.knownHostsFile,
            .strictHostKey = config.strictHostKey,
            .connectTimeout = config.remoteConnectTimeout
        };

        SshClient ssh { std::move(sshConfig) };

        Logger::info("Connecting to remote server.");
        ssh.connect();

        if (config.remoteProvisionEnabled) {
            Logger::info("Remote provisioning is enabled. Preparing server before backup.");
            provisionRemoteServer(ssh, config);
        }

        Logger::info("Creating remote backup.");
        const auto backupResult = ssh.execute(remoteCommand);

        if (backupResult.exitCode != 0) {
            throw std::runtime_error("Remote backup failed: " + backupResult.errorOutput);
        }

        Logger::info("Downloading backup to: " + localFile.string());
        ssh.downloadFile(remoteFile, localFile);

        removeRemoteFile(ssh, config, remoteFile);

        Logger::success("Remote backup completed successfully.");
        Logger::success("Downloaded file: " + localFile.string());

        return 0;
    } catch (const std::exception& exception) {
        Logger::error(exception.what());
        return 1;
    }
}

} // namespace vaultium::remote