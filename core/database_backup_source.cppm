module;

#include <filesystem>
#include <string>

export module vaultium_core_database_backup_source;

import vaultium_core_types;
import vaultium_core_backup_config;
import vaultium_core_backup_source;

export namespace vaultium {

    /**
     * @brief Backup source that adapts the database backup engines
     *        (MySQL/MariaDB, PostgreSQL, SQLite) to the IBackupSource interface.
     */
    class DatabaseBackupSource final : public IBackupSource {
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
