module;

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>

module vaultium_remote_provisioner;

import vaultium_core_logger;
import vaultium_core_types;
import vaultium_core_backup_config;
import vaultium_remote_ssh_client;

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

[[nodiscard]] auto buildMysqlDefaultsFileContent(const BackupConfig& config) -> std::string
{
    std::ostringstream stream;
    stream << "[client]\n"
        << "user=" << config.mysqlUser << '\n'
        << "password=" << config.mysqlPassword << '\n'
        << "host=" << config.mysqlHost << '\n'
        << "port=" << config.mysqlPort << '\n';
    return stream.str();
}

auto ensureCommandSuccess(const SshCommandResult& result, const std::string& action) -> void
{
    if (result.exitCode != 0) {
        throw std::runtime_error(action + " failed: "
            + (result.errorOutput.empty() ? result.output : result.errorOutput));
    }
}

auto testBaseTools(SshClient& ssh, const BackupConfig& config) -> void
{
    Logger::info("Checking base tools on remote server.");
    std::ostringstream command;
    command << "set -e; command -v " << shellQuote(config.gzipPath) << " >/dev/null; ";

    if (config.sourceType == SourceType::Filesystem || config.sourceType == SourceType::ServiceConfig
        || config.engineType == BackupEngineType::Sqlite) {
        command << "command -v " << shellQuote(config.tarPath) << " >/dev/null; ";
    }

    if (config.sourceType == SourceType::Database) {
        switch (config.engineType) {
        case BackupEngineType::MySql:
            command << "(command -v mariadb-dump || command -v " << shellQuote(config.mysqldumpPath)
                << " || command -v mysqldump) >/dev/null; ";
            break;
        case BackupEngineType::PostgreSql:
            command << "command -v " << shellQuote(config.databaseMode == DatabaseMode::All
                ? config.pgDumpAllPath : config.pgDumpPath) << " >/dev/null; ";
            break;
        case BackupEngineType::Sqlite:
            command << "command -v " << shellQuote(config.sqlite3Path) << " >/dev/null; ";
            break;
        }
    }

    ensureCommandSuccess(ssh.execute(command.str()), "Remote tool check");
}

auto testMysqlAccess(SshClient& ssh, const BackupConfig& config) -> void
{
    if (config.sourceType != SourceType::Database || config.engineType != BackupEngineType::MySql) return;
    if (config.mysqlDefaultsFile.empty()) {
        throw std::runtime_error("MYSQL_DEFAULTS_FILE is required when remote MySQL provisioning is enabled.");
    }

    Logger::info("Testing remote MySQL dump access.");
    const auto command = "set -e; DUMP=\"$(command -v mariadb-dump || command -v " + shellQuote(config.mysqldumpPath)
        + " || command -v mysqldump)\"; \"$DUMP\" --defaults-extra-file="
        + shellQuote(config.mysqlDefaultsFile.string()) + " --single-transaction --no-data --all-databases >/dev/null";
    ensureCommandSuccess(ssh.execute(command), "Remote MySQL access test");
}

auto createRemoteDirectories(SshClient& ssh, const BackupConfig& config) -> void
{
    Logger::info("Creating remote Vaultium directories.");
    const auto command = "set -e; mkdir -p " + shellQuote(config.remoteProvisionConfigDirectory.string())
        + "; chmod 700 " + shellQuote(config.remoteProvisionConfigDirectory.string())
        + "; mkdir -p " + shellQuote(config.remoteServerBackupDirectory.string())
        + "; chmod 700 " + shellQuote(config.remoteServerBackupDirectory.string());
    ensureCommandSuccess(ssh.execute(command), "Remote directory creation");
}

auto provisionMysql(SshClient& ssh, const BackupConfig& config) -> void
{
    if (config.sourceType != SourceType::Database || config.engineType != BackupEngineType::MySql) return;
    if (config.mysqlDefaultsFile.empty() || !config.mysqlDefaultsFile.is_absolute()) {
        throw std::runtime_error("An absolute MYSQL_DEFAULTS_FILE is required for remote MySQL provisioning.");
    }

    Logger::info("Provisioning remote MySQL defaults file.");
    ssh.uploadTextFile(config.mysqlDefaultsFile, buildMysqlDefaultsFileContent(config), 0600);
    ensureCommandSuccess(
        ssh.execute("chmod 600 " + shellQuote(config.mysqlDefaultsFile.string())),
        "Remote MySQL defaults file permission update");
}

} // namespace

auto provisionRemoteServer(SshClient& ssh, const BackupConfig& config) -> void
{
    createRemoteDirectories(ssh, config);
    if (config.remoteProvisionEnabled) provisionMysql(ssh, config);
    testBaseTools(ssh, config);
    if (config.remoteProvisionEnabled) testMysqlAccess(ssh, config);
}

auto runRemoteTest(const BackupConfig& config) -> int
{
    try {
        Logger::info("Remote test started.");
        Logger::info("Host: " + config.remoteHost);
        Logger::info("User: " + config.remoteUser);
        SshClient ssh { makeSshConfig(config) };
        Logger::info("Connecting to remote server.");
        ssh.connect();
        testBaseTools(ssh, config);
        Logger::success("Remote test completed successfully.");
        return 0;
    } catch (const std::exception& exception) {
        Logger::error(exception.what());
        return 1;
    }
}

auto runRemoteProvision(const BackupConfig& config) -> int
{
    try {
        Logger::info("Remote provisioning started.");
        Logger::info("Host: " + config.remoteHost);
        Logger::info("User: " + config.remoteUser);
        SshClient ssh { makeSshConfig(config) };
        Logger::info("Connecting to remote server.");
        ssh.connect();
        provisionRemoteServer(ssh, config);
        Logger::success("Remote provisioning completed successfully.");
        return 0;
    } catch (const std::exception& exception) {
        Logger::error(exception.what());
        return 1;
    }
}

} // namespace vaultium::remote
