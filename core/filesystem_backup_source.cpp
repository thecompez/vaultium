module;

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

module vaultium_core_filesystem_backup_source;

import vaultium_core_logger;
import vaultium_core_process_runner;

namespace vaultium {
namespace {

// All configured paths must share one filesystem root so they can be archived
// with a single `tar -C <root>` invocation and restored consistently.
[[nodiscard]] auto commonRoot(const BackupConfig& config) -> std::filesystem::path
{
    auto root = config.backupPaths.front().root_path();

    if (root.empty()) {
        root = "/";
    }

    for (const auto& path : config.backupPaths) {
        if (!path.is_absolute()) {
            throw std::runtime_error("Filesystem backup paths must be absolute: " + path.string());
        }

        auto pathRoot = path.root_path();

        if (pathRoot.empty()) {
            pathRoot = "/";
        }

        if (pathRoot != root) {
            throw std::runtime_error("All BACKUP_PATHS must share the same filesystem root.");
        }
    }

    return root;
}

[[nodiscard]] auto isCompressedArchive(const std::filesystem::path& archive) -> bool
{
    return archive.string().ends_with(".gz");
}

} // namespace

auto FilesystemBackupSource::type() const -> SourceType
{
    return SourceType::Filesystem;
}

auto FilesystemBackupSource::name() const -> std::string
{
    return "filesystem";
}

auto FilesystemBackupSource::artifactPrefix(const BackupConfig& config) const -> std::string
{
    static_cast<void>(config);
    return "files_";
}

auto FilesystemBackupSource::artifactExtension(const BackupConfig& config) const -> std::string
{
    return config.compress ? ".tar.gz" : ".tar";
}

auto FilesystemBackupSource::createBackup(
    const BackupConfig& config,
    const std::filesystem::path& temporaryFile
) const -> BackupArtifact
{
    const auto root = commonRoot(config);

    std::vector<std::string> args {
        config.compress ? "-czf" : "-cf",
        temporaryFile.string(),
        "-C",
        root.string()
    };

    // Archive each path relative to the shared root so restores land back in
    // the same location (e.g. "/etc/myapp" -> "etc/myapp" under "-C /").
    for (const auto& path : config.backupPaths) {
        const auto relative = path.relative_path();

        if (relative.empty()) {
            throw std::runtime_error("Cannot archive the filesystem root itself: " + path.string());
        }

        args.push_back(relative.generic_string());
    }

    const auto result = runProcessToFile(config.tarPath, args, "/dev/null");

    if (result.exitCode != 0) {
        throw std::runtime_error("tar archive failed with exit code: " + std::to_string(result.exitCode));
    }

    if (!std::filesystem::exists(temporaryFile)) {
        throw std::runtime_error("Filesystem archive was not created.");
    }

    if (config.compress && config.validateGzip) {
        if (!validateGzipFile(config.gzipPath, temporaryFile)) {
            throw std::runtime_error("Filesystem archive gzip validation failed.");
        }
    }

    return BackupArtifact {
        .path = temporaryFile,
        .size = std::filesystem::file_size(temporaryFile)
    };
}

auto FilesystemBackupSource::restore(
    const BackupConfig& config,
    const std::filesystem::path& archive,
    const RestoreOptions& options
) const -> void
{
    if (!std::filesystem::exists(archive)) {
        throw std::runtime_error("Backup archive not found: " + archive.string());
    }

    if (options.destination.empty()) {
        throw std::runtime_error("Restore destination is required for filesystem restore.");
    }

    const auto compressed = isCompressedArchive(archive);

    if (options.dryRun) {
        // List the archive contents to confirm it is readable, extract nothing.
        const std::vector<std::string> listArgs {
            compressed ? "-tzf" : "-tf",
            archive.string()
        };

        const auto listResult = runProcessToFile(config.tarPath, listArgs, "/dev/null");

        if (listResult.exitCode != 0) {
            throw std::runtime_error("Dry-run failed: archive is not readable.");
        }

        Logger::info("Dry run: archive is valid and would extract to " + options.destination.string());
        return;
    }

    if (!options.overwrite
        && std::filesystem::exists(options.destination)
        && !std::filesystem::is_empty(options.destination)) {
        throw std::runtime_error(
            "Restore destination is not empty: " + options.destination.string() +
            ". Pass overwrite to extract into it anyway."
        );
    }

    std::filesystem::create_directories(options.destination);

    const std::vector<std::string> extractArgs {
        compressed ? "-xzf" : "-xf",
        archive.string(),
        "-C",
        options.destination.string()
    };

    const auto result = runProcessToFile(config.tarPath, extractArgs, "/dev/null");

    if (result.exitCode != 0) {
        throw std::runtime_error("tar restore failed with exit code: " + std::to_string(result.exitCode));
    }

    Logger::success("Filesystem restore completed into " + options.destination.string());
}

auto FilesystemBackupSource::verify(
    const BackupConfig& config,
    const std::filesystem::path& archive
) const -> bool
{
    if (!std::filesystem::exists(archive) || std::filesystem::file_size(archive) == 0) {
        return false;
    }

    // Listing the archive validates both the compression layer (when present)
    // and the tar structure end-to-end.
    const std::vector<std::string> listArgs {
        isCompressedArchive(archive) ? "-tzf" : "-tf",
        archive.string()
    };

    return runProcessToFile(config.tarPath, listArgs, "/dev/null").exitCode == 0;
}

} // namespace vaultium
