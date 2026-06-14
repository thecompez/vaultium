module;

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

module vaultium_core_service_config_backup_source;

import vaultium_core_types;
import vaultium_core_logger;
import vaultium_core_service_catalog;

namespace vaultium {
namespace {

struct ResolvedPaths {
    std::vector<std::filesystem::path> included;
    std::vector<std::string> includedDisplay;
    std::vector<std::string> skippedDisplay;
};

[[nodiscard]] auto pathExists(const std::filesystem::path& path) -> bool
{
    std::error_code error;
    return std::filesystem::exists(path, error) && !error;
}

// Resolves the configured services (and any explicit extra paths) to the set of
// config paths that actually exist. Missing optional paths are recorded as
// skipped, not fatal.
[[nodiscard]] auto resolvePaths(const BackupConfig& config) -> ResolvedPaths
{
    ResolvedPaths resolved;

    const auto consider = [&](const std::filesystem::path& candidate, const std::string& group) {
        if (pathExists(candidate)) {
            Logger::info("Including " + group + " config path: " + candidate.string());
            resolved.included.push_back(candidate);
            resolved.includedDisplay.push_back(candidate.string());
        } else {
            Logger::warning("Skipping missing " + group + " config path: " + candidate.string());
            resolved.skippedDisplay.push_back(candidate.string());
        }
    };

    // Relocate curated absolute paths under a sandbox prefix when configured
    // (mounted volume, chroot, or test fixture).
    const auto relocate = [&](const std::filesystem::path& candidate) -> std::filesystem::path {
        if (config.serviceRootPrefix.empty()) {
            return candidate;
        }

        return config.serviceRootPrefix / candidate.relative_path();
    };

    for (const auto& service : config.services) {
        const auto definition = serviceDefinition(service);

        for (const auto& candidate : definition.candidatePaths) {
            consider(relocate(candidate), definition.canonicalName);
        }
    }

    for (const auto& extra : config.serviceExtraPaths) {
        consider(extra, "extra");
    }

    return resolved;
}

[[nodiscard]] auto joinServices(const BackupConfig& config) -> std::string
{
    std::string result;

    for (const auto& service : config.services) {
        if (!result.empty()) {
            result += ",";
        }

        result += canonicalServiceName(service);
    }

    return result;
}

} // namespace

auto ServiceConfigBackupSource::type() const -> SourceType
{
    return SourceType::ServiceConfig;
}

auto ServiceConfigBackupSource::name() const -> std::string
{
    return "service-config";
}

auto ServiceConfigBackupSource::artifactPrefix(const BackupConfig& config) const -> std::string
{
    if (config.services.size() == 1) {
        return "service_" + canonicalServiceName(config.services.front()) + "_";
    }

    return "service_";
}

auto ServiceConfigBackupSource::artifactExtension(const BackupConfig& config) const -> std::string
{
    return m_filesystem.artifactExtension(config);
}

auto ServiceConfigBackupSource::createBackup(
    const BackupConfig& config,
    const std::filesystem::path& temporaryFile
) const -> BackupArtifact
{
    const auto resolved = resolvePaths(config);

    if (resolved.included.empty()) {
        throw std::runtime_error(
            "No configuration paths were found for the selected services. "
            "Nothing to back up."
        );
    }

    // Delegate archiving to the filesystem source over the resolved paths.
    BackupConfig delegated = config;
    delegated.backupPaths = resolved.included;

    auto artifact = m_filesystem.createBackup(delegated, temporaryFile);

    artifact.metadata = ArtifactMetadata {
        .present = true,
        .detail = joinServices(config),
        .includedPaths = resolved.includedDisplay,
        .skippedPaths = resolved.skippedDisplay
    };

    return artifact;
}

auto ServiceConfigBackupSource::restore(
    const BackupConfig& config,
    const std::filesystem::path& archive,
    const RestoreOptions& options
) const -> void
{
    // Service-config restore is destructive against live config trees, so it
    // defaults to a dry run unless the caller explicitly requests overwrite.
    RestoreOptions effective = options;

    if (!effective.overwrite && !effective.dryRun) {
        Logger::warning(
            "Service-config restore defaults to a dry run. Pass overwrite to apply changes."
        );
        effective.dryRun = true;
    }

    m_filesystem.restore(config, archive, effective);
}

auto ServiceConfigBackupSource::verify(
    const BackupConfig& config,
    const std::filesystem::path& archive
) const -> bool
{
    return m_filesystem.verify(config, archive);
}

} // namespace vaultium
