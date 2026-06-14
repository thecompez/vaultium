module;

#include <filesystem>
#include <string>

export module vaultium_core_backup_manager;

import vaultium_core_types;
import vaultium_core_backup_config;
import vaultium_core_backup_source;

export namespace vaultium {

    /**
     * @brief Manages backup lifecycle for any backup source.
     *
     * Source-agnostic: it selects an IBackupSource from the configuration and
     * drives backup, retention, restore, and verification uniformly. All logic
     * is UI-independent so the CLI and a future GUI share the same engine.
     */
    class BackupManager final {
    public:
        /**
         * @brief Constructs backup manager.
         *
         * @param config Backup configuration.
         */
        explicit BackupManager(BackupConfig config);

        /**
         * @brief Runs a single backup.
         */
        auto runOnce() -> void;

        /**
         * @brief Runs continuous backup loop.
         */
        auto runLoop() -> void;

        /**
         * @brief Restores a backup artifact.
         *
         * @param archive Artifact to restore.
         * @param options Restore options.
         */
        auto restore(
            const std::filesystem::path& archive,
            const RestoreOptions& options
        ) -> void;

        /**
         * @brief Verifies an artifact's integrity (checksum + structure).
         *
         * @param archive Artifact to verify.
         * @return true when the artifact is intact.
         */
        [[nodiscard]] auto verify(const std::filesystem::path& archive) -> bool;

    private:
        BackupConfig m_config;

        [[nodiscard]] auto createBackupFileName(const IBackupSource& source) const -> std::filesystem::path;
        [[nodiscard]] auto createTimestamp() const -> std::string;
        [[nodiscard]] auto writeChecksumSidecar(const std::filesystem::path& artifact) const -> std::string;
        auto writeMetadataSidecar(
            const std::filesystem::path& artifact,
            const std::string& sourceName,
            const std::string& checksum,
            const ArtifactMetadata& metadata
        ) const -> void;

        auto ensureBackupDirectory() const -> void;
        auto cleanupOldBackups() const -> void;
        auto removeFileIfExists(const std::filesystem::path& path) const -> void;
    };

} // namespace vaultium
