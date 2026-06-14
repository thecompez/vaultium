module;

#include <filesystem>
#include <memory>
#include <string>

export module vaultium_core_backup_source;

import vaultium_core_types;
import vaultium_core_backup_config;

export namespace vaultium {

    /**
     * @brief Options controlling a restore operation.
     */
    struct RestoreOptions {
        // Where the backup should be restored. For filesystem backups this is the
        // directory the archive is extracted into.
        std::filesystem::path destination;

        // Allow restoring over existing files/directories.
        bool overwrite { false };

        // Validate and report actions without writing anything.
        bool dryRun { false };
    };

    /**
     * @brief A backup source: something Vaultium can back up, restore, and verify.
     *
     * This is the central extension point. Database, filesystem, and (future)
     * service-config backups all implement this interface so the manager, CLI,
     * and any future GUI consume one uniform contract.
     *
     * Implementations must not invoke a shell; they use safe argv-based process
     * execution or in-process logic only.
     */
    class IBackupSource {
    public:
        virtual ~IBackupSource() = default;

        /**
         * @brief The source category this implementation handles.
         */
        [[nodiscard]] virtual auto type() const -> SourceType = 0;

        /**
         * @brief Human-readable source name, used in logs and filenames.
         */
        [[nodiscard]] virtual auto name() const -> std::string = 0;

        /**
         * @brief Filename prefix for generated artifacts (e.g. "files_").
         */
        [[nodiscard]] virtual auto artifactPrefix(const BackupConfig& config) const -> std::string = 0;

        /**
         * @brief Filename extension for generated artifacts (e.g. ".tar.gz").
         */
        [[nodiscard]] virtual auto artifactExtension(const BackupConfig& config) const -> std::string = 0;

        /**
         * @brief Produces a backup into the given temporary file.
         *
         * @param config Backup configuration.
         * @param temporaryFile Destination temporary artifact path.
         * @return The produced artifact (path and size).
         */
        [[nodiscard]] virtual auto createBackup(
            const BackupConfig& config,
            const std::filesystem::path& temporaryFile
        ) const -> BackupArtifact = 0;

        /**
         * @brief Restores a previously produced artifact.
         *
         * @param config Backup configuration.
         * @param archive Path to the backup artifact to restore.
         * @param options Restore options (destination, overwrite, dry-run).
         */
        virtual auto restore(
            const BackupConfig& config,
            const std::filesystem::path& archive,
            const RestoreOptions& options
        ) const -> void = 0;

        /**
         * @brief Verifies artifact integrity (format/structure level).
         *
         * Checksum verification is handled uniformly by the manager; this method
         * covers source-specific structural checks (e.g. gzip/tar integrity).
         *
         * @param config Backup configuration.
         * @param archive Path to the backup artifact.
         * @return true when the artifact is structurally valid.
         */
        [[nodiscard]] virtual auto verify(
            const BackupConfig& config,
            const std::filesystem::path& archive
        ) const -> bool = 0;
    };

    /**
     * @brief Constructs the backup source selected by the configuration.
     *
     * @param config Backup configuration.
     * @return Owned backup source implementation.
     * @throws std::runtime_error for unsupported source types.
     */
    [[nodiscard]] auto createBackupSource(const BackupConfig& config) -> std::unique_ptr<IBackupSource>;

} // namespace vaultium
