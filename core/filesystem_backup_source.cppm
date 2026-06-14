module;

#include <filesystem>
#include <string>

export module vaultium_core_filesystem_backup_source;

import vaultium_core_types;
import vaultium_core_backup_config;
import vaultium_core_backup_source;

export namespace vaultium {

    /**
     * @brief Backup source for arbitrary files and directories.
     *
     * Archives the configured BACKUP_PATHS into a tar (optionally gzip) artifact
     * using safe argv-based process execution. Paths are archived relative to the
     * filesystem root so they restore back to their original locations (or any
     * chosen destination prefix).
     */
    class FilesystemBackupSource final : public IBackupSource {
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
    };

} // namespace vaultium
