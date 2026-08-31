module;

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

module vaultium_remote_client;

import vaultium_core_logger;
import vaultium_core_types;
import vaultium_core_backup_config;
import vaultium_core_backup_source;
import vaultium_core_service_catalog;
import vaultium_core_sha256;
import vaultium_remote_ssh_client;
import vaultium_remote_provisioner;

namespace vaultium::remote {
namespace {

[[nodiscard]] auto shellQuote(const std::string& value) -> std::string
{
    std::string result { "'" };
    for (const char character : value) {
        if (character == '\'') result += "'\\''";
        else result.push_back(character);
    }
    result.push_back('\'');
    return result;
}

[[nodiscard]] auto sqliteQuote(const std::string& value) -> std::string
{
    std::string result { "'" };
    for (const char character : value) {
        if (character == '\'') result += "''";
        else result.push_back(character);
    }
    result.push_back('\'');
    return result;
}

[[nodiscard]] auto timestamp() -> std::string
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count() % 1'000'000;
    std::tm localTime {};
    localtime_r(&time, &localTime);
    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y-%m-%d_%H-%M-%S")
        << '-' << std::setw(6) << std::setfill('0') << micros;
    return stream.str();
}

[[nodiscard]] auto joinCommandParts(const std::vector<std::string>& parts) -> std::string
{
    std::ostringstream stream;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index > 0) stream << ' ';
        stream << parts[index];
    }
    return stream.str();
}

[[nodiscard]] auto commonRemotePrefix(const BackupConfig& config) -> std::string
{
    return "set -e; mkdir -p " + shellQuote(config.remoteServerBackupDirectory.string())
        + "; chmod 700 " + shellQuote(config.remoteServerBackupDirectory.string()) + "; ";
}

[[nodiscard]] auto pipefailPrefix() -> std::string
{
    return "(set -o pipefail) 2>/dev/null && set -o pipefail || true; ";
}

[[nodiscard]] auto buildMysqlRemoteCommand(const BackupConfig& config, const std::string& remoteFile) -> std::string
{
    std::vector<std::string> dumpParts { "\"$VAULTIUM_DUMP\"" };
    if (!config.mysqlDefaultsFile.empty()) {
        dumpParts.push_back("--defaults-extra-file=" + shellQuote(config.mysqlDefaultsFile.string()));
    }
    dumpParts.insert(dumpParts.end(), {
        "--single-transaction", "--routines", "--triggers", "--events", "--hex-blob",
        "--default-character-set=utf8mb4", "$GTID"
    });
    if (config.databaseMode == DatabaseMode::All) {
        dumpParts.emplace_back("--all-databases");
    } else {
        dumpParts.emplace_back("--databases");
        for (const auto& database : config.databases) dumpParts.push_back(shellQuote(database));
    }

    const auto detectDump =
        "VAULTIUM_DUMP=\"$(command -v mariadb-dump || command -v " + shellQuote(config.mysqldumpPath)
        + " || command -v mysqldump || true)\"; [ -n \"$VAULTIUM_DUMP\" ]; "
          "case \"$VAULTIUM_DUMP\" in *mariadb*) GTID=\"\";; *) GTID=\"--set-gtid-purged=OFF\";; esac; ";
    const auto dump = detectDump + joinCommandParts(dumpParts);

    std::ostringstream command;
    command << commonRemotePrefix(config) << pipefailPrefix();
    if (config.compress) {
        command << dump << " | " << shellQuote(config.gzipPath) << " -c > " << shellQuote(remoteFile)
            << "; " << shellQuote(config.gzipPath) << " -t " << shellQuote(remoteFile) << "; ";
    } else {
        command << dump << " > " << shellQuote(remoteFile) << "; ";
    }
    command << "test -s " << shellQuote(remoteFile) << "; chmod 600 " << shellQuote(remoteFile);
    return command.str();
}

[[nodiscard]] auto buildPostgreSqlRemoteCommand(const BackupConfig& config, const std::string& remoteFile) -> std::string
{
    std::string dump;
    if (config.databaseMode == DatabaseMode::All) {
        dump = shellQuote(config.pgDumpAllPath)
            + " --host=" + shellQuote(config.postgresHost)
            + " --port=" + shellQuote(config.postgresPort)
            + " --username=" + shellQuote(config.postgresUser)
            + " --clean --if-exists --no-password";
    } else {
        if (config.databases.size() != 1) {
            throw std::runtime_error("Remote PostgreSQL selected mode supports one database per job.");
        }
        dump = shellQuote(config.pgDumpPath)
            + " --host=" + shellQuote(config.postgresHost)
            + " --port=" + shellQuote(config.postgresPort)
            + " --username=" + shellQuote(config.postgresUser)
            + " --format=plain --clean --if-exists --no-password " + shellQuote(config.databases.front());
    }

    std::ostringstream command;
    command << commonRemotePrefix(config) << pipefailPrefix()
        << "PGPASSWORD=$(cat " << shellQuote(config.postgresPasswordFile.string()) << "); export PGPASSWORD; ";
    if (config.compress) {
        command << dump << " | " << shellQuote(config.gzipPath) << " -c > " << shellQuote(remoteFile)
            << "; " << shellQuote(config.gzipPath) << " -t " << shellQuote(remoteFile) << "; ";
    } else {
        command << dump << " > " << shellQuote(remoteFile) << "; ";
    }
    command << "unset PGPASSWORD; test -s " << shellQuote(remoteFile) << "; chmod 600 " << shellQuote(remoteFile);
    return command.str();
}

[[nodiscard]] auto buildSqliteRemoteCommand(const BackupConfig& config, const std::string& remoteFile) -> std::string
{
    if (config.sqliteFiles.empty()) throw std::runtime_error("SQLITE_FILES cannot be empty for remote SQLite backup.");
    const auto temporaryDirectory = config.remoteServerBackupDirectory.string() + "/.sqlite-snapshot-" + timestamp();
    std::unordered_set<std::string> names;

    std::ostringstream command;
    command << commonRemotePrefix(config)
        << "rm -rf " << shellQuote(temporaryDirectory) << "; mkdir -m 700 " << shellQuote(temporaryDirectory) << "; ";

    for (const auto& sqliteFile : config.sqliteFiles) {
        if (!sqliteFile.is_absolute()) throw std::runtime_error("Remote SQLite path must be absolute: " + sqliteFile.string());
        const auto name = sqliteFile.filename().string();
        if (name.empty() || !names.insert(name).second) {
            throw std::runtime_error("Remote SQLite backup contains duplicate or invalid filenames: " + name);
        }
        const auto target = temporaryDirectory + "/" + name;
        const auto backupCommand = ".backup " + sqliteQuote(target);
        command << shellQuote(config.sqlite3Path) << ' ' << shellQuote(sqliteFile.string()) << ' '
            << shellQuote(backupCommand) << "; test -s " << shellQuote(target) << "; ";
    }

    command << shellQuote(config.tarPath) << ' ' << (config.compress ? "-czf " : "-cf ")
        << shellQuote(remoteFile) << " -C " << shellQuote(temporaryDirectory) << " .; "
        << "rm -rf " << shellQuote(temporaryDirectory) << "; ";
    if (config.compress) command << shellQuote(config.gzipPath) << " -t " << shellQuote(remoteFile) << "; ";
    command << "test -s " << shellQuote(remoteFile) << "; chmod 600 " << shellQuote(remoteFile);
    return command.str();
}

[[nodiscard]] auto buildFilesystemRemoteCommand(const BackupConfig& config, const std::string& remoteFile) -> std::string
{
    if (config.backupPaths.empty()) throw std::runtime_error("BACKUP_PATHS cannot be empty for remote filesystem backup.");
    auto root = config.backupPaths.front().root_path();
    if (root.empty()) root = "/";

    std::ostringstream command;
    command << commonRemotePrefix(config) << shellQuote(config.tarPath) << ' '
        << (config.compress ? "-czf " : "-cf ") << shellQuote(remoteFile) << " -C " << shellQuote(root.string());
    for (const auto& path : config.backupPaths) {
        if (!path.is_absolute()) throw std::runtime_error("Remote filesystem backup path must be absolute: " + path.string());
        if (path.root_path() != root) throw std::runtime_error("All BACKUP_PATHS must share the same filesystem root.");
        command << ' ' << shellQuote(path.relative_path().generic_string());
    }
    command << "; ";
    if (config.compress) command << shellQuote(config.gzipPath) << " -t " << shellQuote(remoteFile) << "; ";
    command << "test -s " << shellQuote(remoteFile) << "; chmod 600 " << shellQuote(remoteFile);
    return command.str();
}

[[nodiscard]] auto buildServiceConfigRemoteCommand(const BackupConfig& config, const std::string& remoteFile) -> std::string
{
    if (config.services.empty() && config.serviceExtraPaths.empty()) {
        throw std::runtime_error("BACKUP_SERVICES or BACKUP_SERVICE_EXTRA_PATHS is required for remote service-config backup.");
    }

    std::vector<std::string> candidates;
    for (const auto& service : config.services) {
        if (!isKnownService(service)) throw std::runtime_error("Unknown service: " + service);
        for (const auto& path : serviceDefinition(service).candidatePaths) candidates.push_back(path.string());
    }
    for (const auto& path : config.serviceExtraPaths) candidates.push_back(path.string());

    std::ostringstream command;
    command << commonRemotePrefix(config) << "set --; for p in";
    for (const auto& candidate : candidates) command << ' ' << shellQuote(candidate);
    command << "; do [ -e \"$p\" ] && set -- \"$@\" \"$p\"; done; "
        << "[ \"$#\" -gt 0 ] || { echo 'No selected service configuration paths exist.' >&2; exit 1; }; "
        << shellQuote(config.tarPath) << ' ' << (config.compress ? "-czf " : "-cf ")
        << shellQuote(remoteFile) << " -P \"$@\"; ";
    if (config.compress) command << shellQuote(config.gzipPath) << " -t " << shellQuote(remoteFile) << "; ";
    command << "test -s " << shellQuote(remoteFile) << "; chmod 600 " << shellQuote(remoteFile);
    return command.str();
}

[[nodiscard]] auto buildRemoteCommand(const BackupConfig& config, const std::string& remoteFile) -> std::string
{
    if (config.sourceType == SourceType::Filesystem) return buildFilesystemRemoteCommand(config, remoteFile);
    if (config.sourceType == SourceType::ServiceConfig) return buildServiceConfigRemoteCommand(config, remoteFile);
    switch (config.engineType) {
    case BackupEngineType::MySql: return buildMysqlRemoteCommand(config, remoteFile);
    case BackupEngineType::PostgreSql: return buildPostgreSqlRemoteCommand(config, remoteFile);
    case BackupEngineType::Sqlite: return buildSqliteRemoteCommand(config, remoteFile);
    }
    throw std::runtime_error("Unsupported remote backup engine.");
}

[[nodiscard]] auto makeSshConfig(const BackupConfig& config) -> SshConnectionConfig
{
    return {
        .host = config.remoteHost,
        .port = config.remotePort,
        .username = config.remoteUser,
        .authMethod = config.remoteAuthMethod,
        .privateKeyPath = config.remoteIdentityFile,
        .privateKeyPassphrase = config.remoteIdentityPassphrase,
        .password = config.remotePassword,
        .knownHostsFile = config.knownHostsFile,
        .strictHostKey = config.strictHostKey,
        .connectTimeout = config.remoteConnectTimeout,
        .commandTimeout = config.remoteCommandTimeout
    };
}

[[nodiscard]] auto uniqueLocalPath(const std::filesystem::path& directory, const std::string& fileName)
    -> std::filesystem::path
{
    const auto requested = directory / fileName;
    if (!std::filesystem::exists(requested)) return requested;

    const auto extension = requested.extension().string();
    const auto stem = requested.stem().string();
    for (std::size_t index = 1; index < 10000; ++index) {
        const auto candidate = directory / (stem + "_" + std::to_string(index) + extension);
        if (!std::filesystem::exists(candidate)
            && !std::filesystem::exists(std::filesystem::path { candidate.string() + ".sha256" })) return candidate;
    }
    throw std::runtime_error("Could not allocate a unique local remote-backup filename.");
}

auto writeChecksumSidecar(const std::filesystem::path& artifact) -> void
{
    const auto checksum = sha256File(artifact);
    const auto sidecar = std::filesystem::path { artifact.string() + ".sha256" };
    const auto temporary = std::filesystem::path { sidecar.string() + ".tmp" };
    {
        std::ofstream output { temporary, std::ios::trunc };
        if (!output) throw std::runtime_error("Could not write remote-backup checksum sidecar.");
        output << checksum << "  " << artifact.filename().string() << '\n';
        output.close();
        if (!output) throw std::runtime_error("Could not finalize remote-backup checksum sidecar.");
    }
    std::filesystem::permissions(temporary,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace);
    std::filesystem::rename(temporary, sidecar);
}

auto removeRemoteFile(SshClient& ssh, const BackupConfig& config, const std::string& remoteFile) -> void
{
    if (!config.remoteRemoveAfterDownload) return;
    const auto result = ssh.execute("rm -f -- " + shellQuote(remoteFile));
    if (result.exitCode != 0) Logger::warning("Could not remove remote temporary file: " + result.errorOutput);
}

} // namespace

auto runRemoteBackup(const BackupConfig& config) -> int
{
    std::filesystem::path temporaryLocal;
    std::filesystem::path finalLocal;
    try {
        Logger::info("Remote libssh2 backup mode selected.");
        Logger::info("Host: " + config.remoteHost);
        Logger::info("User: " + config.remoteUser);
        Logger::info("Port: " + std::to_string(config.remotePort));

        const auto source = createBackupSource(config);
        const auto fileName = source->artifactPrefix(config) + timestamp() + source->artifactExtension(config);
        const auto remoteFile = (config.remoteServerBackupDirectory / fileName).string();

        std::filesystem::create_directories(config.remoteDownloadDirectory);
        std::filesystem::permissions(config.remoteDownloadDirectory,
            std::filesystem::perms::owner_all,
            std::filesystem::perm_options::replace);
        finalLocal = uniqueLocalPath(config.remoteDownloadDirectory, fileName);
        temporaryLocal = finalLocal.parent_path() / (".vaultium-part-" + finalLocal.filename().string());
        if (std::filesystem::exists(temporaryLocal)) std::filesystem::remove(temporaryLocal);

        SshClient ssh { makeSshConfig(config) };
        Logger::info("Connecting to remote server.");
        ssh.connect();

        if (config.remoteProvisionEnabled) {
            Logger::info("Remote provisioning is enabled. Preparing server before backup.");
            provisionRemoteServer(ssh, config);
        }

        Logger::info("Creating remote backup.");
        const auto backupResult = ssh.execute(buildRemoteCommand(config, remoteFile));
        if (backupResult.exitCode != 0) {
            throw std::runtime_error("Remote backup failed: "
                + (backupResult.errorOutput.empty() ? backupResult.output : backupResult.errorOutput));
        }

        try {
            Logger::info("Downloading backup to a staging artifact.");
            ssh.downloadFile(remoteFile, temporaryLocal);
            if (!source->verify(config, temporaryLocal)) {
                throw std::runtime_error("Downloaded remote backup failed structural verification.");
            }
            if (std::filesystem::exists(finalLocal)) {
                throw std::runtime_error("Refusing to overwrite existing local backup: " + finalLocal.string());
            }
            std::filesystem::rename(temporaryLocal, finalLocal);
            if (config.checksumEnabled) writeChecksumSidecar(finalLocal);
        } catch (...) {
            std::error_code error;
            std::filesystem::remove(temporaryLocal, error);
            std::filesystem::remove(finalLocal, error);
            std::filesystem::remove(std::filesystem::path { finalLocal.string() + ".sha256" }, error);
            throw;
        }

        removeRemoteFile(ssh, config, remoteFile);

        Logger::success("Remote backup completed successfully.");
        Logger::success("Downloaded file: " + finalLocal.string());
        return 0;
    } catch (const std::exception& exception) {
        std::error_code error;
        if (!temporaryLocal.empty()) std::filesystem::remove(temporaryLocal, error);
        Logger::error(exception.what());
        return 1;
    }
}

} // namespace vaultium::remote
