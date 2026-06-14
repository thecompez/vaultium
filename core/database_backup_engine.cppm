module;

#include <chrono>
#include <ctime>
#include <filesystem>
#include <vector>
#include <iostream>
#include <string>

export module vaultium_core_database_backup_engine;

import vaultium_core_types;
import vaultium_core_backup_config;

export namespace vaultium {

    /**
     * @brief Interface for database-specific backup engines.
     */
    class IDatabaseBackupEngine {
    public:
        virtual ~IDatabaseBackupEngine() = default;

        /**
         * @brief Returns engine name.
         *
         * @return Engine name.
         */
        [[nodiscard]] virtual auto name() const -> std::string = 0;

        /**
         * @brief Creates backup into a temporary file.
         *
         * @param config Backup configuration.
         * @param temporaryFile Temporary file path.
         * @return Backup artifact.
         */
        [[nodiscard]] virtual auto createBackup(
            const BackupConfig& config,
            const std::filesystem::path& temporaryFile
        ) const -> BackupArtifact = 0;
    };

} // namespace vaultium