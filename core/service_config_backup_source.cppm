module;

#include <filesystem>
#include <string>

export module vaultium_core_service_config_backup_source;

import vaultium_core_types;
import vaultium_core_backup_config;
import vaultium_core_backup_source;
import vaultium_core_filesystem_backup_source;

export namespace vaultium {

    /**
     * @brief Backup source for curated service configuration trees.
     *
     * A safe specialization over FilesystemBackupSource: it resolves the
     * configured services (nginx, apache, systemd, docker, mysql, postgresql)
     * to their curated config paths, skips missing optional paths with a
     * warning, and fails only if nothing at all is present. It then delegates
     * archiving/verify/restore to the filesystem source and attaches metadata
     * describing what was included and skipped.
     *
     * Restore defaults to a dry run unless overwrite is explicitly requested.
     */
    class ServiceConfigBackupSource final : public IBackupSource {
    public:
        [[nodiscard]] auto type() const -> SourceType override;
        [[nodiscard]] auto name() const -> std::string override;

        [[nodiscard]] auto artifactPrefix(const BackupConfig& config) const -> std::string override;
        [[nodiscard]] auto artifactExtension(const BackupConfig& config) const -> std::string override;

        [[nodiscard]] auto createBackup(
            const BackupConfig& config,
            const std::filesystem::path& temporaryFile
        ) const -> BackupArtifact override;

        auto restore(
            const BackupConfig& config,
            const std::filesystem::path& archive,
            const RestoreOptions& options
        ) const -> void override;

        [[nodiscard]] auto verify(
            const BackupConfig& config,
            const std::filesystem::path& archive
        ) const -> bool override;

    private:
        FilesystemBackupSource m_filesystem;
    };

} // namespace vaultium
