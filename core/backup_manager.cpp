module;

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(VAULTIUM_PLATFORM_LINUX) || defined(VAULTIUM_PLATFORM_MACOS)
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

module vaultium_core_backup_manager;

import vaultium_core_types;
import vaultium_core_logger;
import vaultium_core_backup_source;
import vaultium_core_sha256;

namespace vaultium {
namespace {

class FileLock final {
public:
    explicit FileLock(const std::filesystem::path& path)
    {
#if defined(VAULTIUM_PLATFORM_LINUX) || defined(VAULTIUM_PLATFORM_MACOS)
        m_fd = ::open(path.c_str(), O_CREAT | O_RDWR, 0600);
        if (m_fd == -1) {
            throw std::runtime_error("Could not open lock file: " + path.string());
        }
        if (::flock(m_fd, LOCK_EX | LOCK_NB) == -1) {
            ::close(m_fd);
            m_fd = -1;
        }
#else
        static_cast<void>(path);
        throw std::runtime_error("File locking is not implemented for this platform yet.");
#endif
    }

    FileLock(const FileLock&) = delete;
    auto operator=(const FileLock&) -> FileLock& = delete;

    FileLock(FileLock&& other) noexcept
        : m_fd { std::exchange(other.m_fd, -1) }
    {
    }

    auto operator=(FileLock&& other) noexcept -> FileLock&
    {
        if (this != &other) {
            release();
            m_fd = std::exchange(other.m_fd, -1);
        }
        return *this;
    }

    ~FileLock() { release(); }

    [[nodiscard]] auto isLocked() const -> bool { return m_fd != -1; }

private:
    int m_fd { -1 };

    auto release() noexcept -> void
    {
#if defined(VAULTIUM_PLATFORM_LINUX) || defined(VAULTIUM_PLATFORM_MACOS)
        if (m_fd != -1) {
            ::flock(m_fd, LOCK_UN);
            ::close(m_fd);
            m_fd = -1;
        }
#endif
    }
};

[[nodiscard]] auto sidecarPath(const std::filesystem::path& artifact, std::string_view suffix)
    -> std::filesystem::path
{
    return std::filesystem::path { artifact.string() + std::string { suffix } };
}

} // namespace

BackupManager::BackupManager(BackupConfig config)
    : m_config { std::move(config) }
{
}

auto BackupManager::runOnce() -> void
{
    if (!m_config.enabled) {
        Logger::warning("Backup is disabled by configuration.");
        return;
    }

    FileLock lock { m_config.lockFile };
    if (!lock.isLocked()) {
        Logger::warning("Another backup process is already running. Current run skipped safely.");
        return;
    }

    const auto source = createBackupSource(m_config);
    ensureBackupDirectory();

    const auto finalFile = createBackupFileName(*source);
    const auto temporaryFile = std::filesystem::path { finalFile.string() + ".tmp" };

    Logger::info("Starting backup.");
    Logger::info("Source: " + source->name());
    Logger::info("Target file: " + finalFile.string());

    removeFileIfExists(temporaryFile);

    try {
        const auto artifact = source->createBackup(m_config, temporaryFile);
        if (!std::filesystem::exists(artifact.path)) {
            throw std::runtime_error("Temporary backup file was not created.");
        }
        if (artifact.size == 0 || std::filesystem::file_size(artifact.path) == 0) {
            throw std::runtime_error("Backup output file is empty.");
        }
        if (std::filesystem::exists(finalFile)) {
            throw std::runtime_error("Refusing to overwrite an existing backup artifact: " + finalFile.string());
        }

        std::filesystem::rename(artifact.path, finalFile);
        std::filesystem::permissions(
            finalFile,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace
        );

        std::string checksum;
        if (m_config.checksumEnabled) {
            checksum = writeChecksumSidecar(finalFile);
            Logger::info("SHA-256: " + checksum);
        }

        if (artifact.metadata.present) {
            if (checksum.empty()) {
                checksum = sha256File(finalFile);
            }
            writeMetadataSidecar(finalFile, source->name(), checksum, artifact.metadata);
            Logger::info("Wrote metadata sidecar.");
        }
    } catch (...) {
        removeFileIfExists(temporaryFile);
        removeFileIfExists(finalFile);
        removeFileIfExists(sidecarPath(finalFile, ".sha256"));
        removeFileIfExists(sidecarPath(finalFile, ".meta.json"));
        throw;
    }

    if (m_config.cleanupEnabled) {
        cleanupOldBackups();
    }

    Logger::success("Backup completed successfully.");
    Logger::success("Backup file: " + finalFile.string());
}

auto BackupManager::runLoop() -> void
{
    Logger::info("Backup loop started.");
    Logger::info("Interval minutes: " + std::to_string(m_config.interval.count()));
    while (true) {
        try {
            runOnce();
        } catch (const std::exception& exception) {
            Logger::error(exception.what());
        }
        Logger::info("Next backup in " + std::to_string(m_config.interval.count()) + " minutes.");
        std::this_thread::sleep_for(m_config.interval);
    }
}

auto BackupManager::restore(
    const std::filesystem::path& archive,
    const RestoreOptions& options
) -> void
{
    const auto source = createBackupSource(m_config);
    Logger::info("Restoring " + archive.string() + " via " + source->name() + " source.");

    if (!verify(archive)) {
        throw std::runtime_error("Refusing to restore: integrity verification failed.");
    }

    source->restore(m_config, archive, options);
}

auto BackupManager::verify(const std::filesystem::path& archive) -> bool
{
    if (!std::filesystem::exists(archive) || !std::filesystem::is_regular_file(archive)) {
        Logger::error("Artifact not found or not a regular file: " + archive.string());
        return false;
    }
    if (std::filesystem::file_size(archive) == 0) {
        Logger::error("Artifact is empty: " + archive.string());
        return false;
    }

    const auto sidecar = sidecarPath(archive, ".sha256");
    if (m_config.checksumEnabled && !std::filesystem::exists(sidecar)) {
        Logger::error("Checksum is required but the .sha256 sidecar is missing.");
        return false;
    }

    if (std::filesystem::exists(sidecar)) {
        std::ifstream sidecarFile { sidecar };
        std::string expected;
        sidecarFile >> expected;
        if (expected.size() != 64) {
            Logger::error("Checksum sidecar is malformed: " + sidecar.string());
            return false;
        }
        const auto actual = sha256File(archive);
        if (expected != actual) {
            Logger::error("Checksum mismatch for " + archive.string());
            Logger::error("Expected: " + expected);
            Logger::error("Actual:   " + actual);
            return false;
        }
        Logger::info("Checksum verified: " + actual);
    } else {
        Logger::warning("No checksum sidecar found; checksum verification is disabled by configuration.");
    }

    const auto source = createBackupSource(m_config);
    if (!source->verify(m_config, archive)) {
        Logger::error("Structural verification failed for " + archive.string());
        return false;
    }

    Logger::success("Artifact verified: " + archive.string());
    return true;
}

auto BackupManager::createBackupFileName(const IBackupSource& source) const -> std::filesystem::path
{
    const auto base = source.artifactPrefix(m_config) + createTimestamp();
    const auto extension = source.artifactExtension(m_config);

    for (std::size_t suffix = 0; suffix < 10000; ++suffix) {
        const auto name = base + (suffix == 0 ? std::string {} : "_" + std::to_string(suffix)) + extension;
        const auto candidate = m_config.backupDirectory / name;
        if (!std::filesystem::exists(candidate)
            && !std::filesystem::exists(sidecarPath(candidate, ".sha256"))
            && !std::filesystem::exists(sidecarPath(candidate, ".meta.json"))
            && !std::filesystem::exists(std::filesystem::path { candidate.string() + ".tmp" })) {
            return candidate;
        }
    }

    throw std::runtime_error("Could not allocate a unique backup artifact name.");
}

auto BackupManager::createTimestamp() const -> std::string
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

auto BackupManager::writeChecksumSidecar(const std::filesystem::path& artifact) const -> std::string
{
    const auto checksum = sha256File(artifact);
    const auto sidecar = sidecarPath(artifact, ".sha256");
    std::ofstream file { sidecar, std::ios::trunc };
    if (!file) {
        throw std::runtime_error("Could not write checksum file: " + sidecar.string());
    }
    file << checksum << "  " << artifact.filename().string() << '\n';
    file.close();
    if (!file) {
        throw std::runtime_error("Could not finalize checksum file: " + sidecar.string());
    }
    std::filesystem::permissions(
        sidecar,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace
    );
    return checksum;
}

auto BackupManager::writeMetadataSidecar(
    const std::filesystem::path& artifact,
    const std::string& sourceName,
    const std::string& checksum,
    const ArtifactMetadata& metadata
) const -> void
{
    const auto escape = [](const std::string& value) {
        std::string result;
        for (const char character : value) {
            switch (character) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\t': result += "\\t"; break;
            case '\r': result += "\\r"; break;
            default:
                if (static_cast<unsigned char>(character) < 0x20) {
                    result += "?";
                } else {
                    result.push_back(character);
                }
            }
        }
        return result;
    };

    const auto jsonArray = [&escape](const std::vector<std::string>& values) {
        std::string result = "[";
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index > 0) result += ", ";
            result += "\"" + escape(values[index]) + "\"";
        }
        result += "]";
        return result;
    };

    const auto sidecar = sidecarPath(artifact, ".meta.json");
    std::ofstream file { sidecar, std::ios::trunc };
    if (!file) {
        throw std::runtime_error("Could not write metadata file: " + sidecar.string());
    }

    file << "{\n"
         << "  \"archive\": \"" << escape(artifact.filename().string()) << "\",\n"
         << "  \"source\": \"" << escape(sourceName) << "\",\n"
         << "  \"detail\": \"" << escape(metadata.detail) << "\",\n"
         << "  \"timestamp\": \"" << escape(createTimestamp()) << "\",\n"
         << "  \"sha256\": \"" << escape(checksum) << "\",\n"
         << "  \"includedPaths\": " << jsonArray(metadata.includedPaths) << ",\n"
         << "  \"skippedPaths\": " << jsonArray(metadata.skippedPaths) << "\n"
         << "}\n";
    file.close();
    if (!file) {
        throw std::runtime_error("Could not finalize metadata file: " + sidecar.string());
    }

    std::filesystem::permissions(
        sidecar,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace
    );
}

auto BackupManager::ensureBackupDirectory() const -> void
{
    std::filesystem::create_directories(m_config.backupDirectory);
    if (!std::filesystem::is_directory(m_config.backupDirectory)) {
        throw std::runtime_error("Backup destination is not a directory: " + m_config.backupDirectory.string());
    }
    std::filesystem::permissions(
        m_config.backupDirectory,
        std::filesystem::perms::owner_all,
        std::filesystem::perm_options::replace
    );
}

auto BackupManager::cleanupOldBackups() const -> void
{
    if (m_config.retentionDays == 0) {
        Logger::warning("Retention cleanup disabled because retention days is zero.");
        return;
    }

    const auto now = std::filesystem::file_time_type::clock::now();
    const auto retentionAge = std::chrono::hours { 24LL * static_cast<long long>(m_config.retentionDays) };

    for (const auto& entry : std::filesystem::directory_iterator(m_config.backupDirectory)) {
        if (!entry.is_regular_file()) continue;
        const auto path = entry.path();
        const auto filename = path.filename().string();
        const auto isKnownBackup = filename.starts_with("mysql_")
            || filename.starts_with("postgresql_")
            || filename.starts_with("sqlite_")
            || filename.starts_with("files_")
            || filename.starts_with("service_")
            || filename.starts_with("database_");
        if (!isKnownBackup || filename.ends_with(".tmp") || filename.ends_with(".sha256")
            || filename.ends_with(".meta.json")) {
            continue;
        }
        if (now - entry.last_write_time() > retentionAge) {
            Logger::info("Removing old backup: " + path.string());
            std::filesystem::remove(path);
            removeFileIfExists(sidecarPath(path, ".sha256"));
            removeFileIfExists(sidecarPath(path, ".meta.json"));
        }
    }
}

auto BackupManager::removeFileIfExists(const std::filesystem::path& path) const -> void
{
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        std::filesystem::remove(path, error);
    }
}

} // namespace vaultium
